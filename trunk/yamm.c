/*
   alloca() is not implemented as it has nothing in common with other methods
*/
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <time.h>  /* For leak check based on time */

#include <yamm.h>
#include <yamm_leak_hp.c>

#define IS_INITIALIZED            {if(1 != Initialized) Init();}
#define ALIGNMENT                 8
#define ELEMENT_HEAD_SIZE         (((sizeof(SElementHead) / ALIGNMENT) + 1) * ALIGNMENT)
#define ELEMENT_SIZE(DataSize)    ((DataSize / ALIGNMENT + 1) * ALIGNMENT + ELEMENT_HEAD_SIZE)

#define ELEMENTS_PER_BLOCK(DataSize) ((8 * 1024L * 1024L - 16 * 1024L - BLOCK_HEAD_SIZE) / ELEMENT_SIZE(DataSize))

/* Block header contains a pointer to the next block */
#define BLOCK_HEAD_SIZE           (((sizeof(void*) / ALIGNMENT) + 1) * ALIGNMENT)
#define BLOCK_SIZE(DataSize)      (ELEMENT_SIZE(DataSize) * ELEMENTS_PER_BLOCK(DataSize) + BLOCK_HEAD_SIZE)

#define EMPTY_ERROR               99999
#define STOP_ERROR_LOOKUP         999999

/* Tuning parameters */
extern int yamm_max_thread_num;
extern int yamm_make_statistics;
extern int yamm_debug_level;
extern int yamm_double_free;
extern int yamm_sizes[];
extern int yamm_is_dumper_on;
extern int yamm_dump_interval;
extern hrtime_t yamm_leak_check_start_time;
extern hrtime_t yamm_leak_check_stop_time;

static int* Sizes = NULL;
static int MaxThreadNum = 0;
static int MakeStatistics = 0;
static int DebugLevel = 0;
static int DoubleFree = 0;
static int IsDumperOn = 0;
static int DumpInterval = 1;
static hrtime_t LeakCheckStartTime = -1;
static hrtime_t LeakCheckStopTime = 3;
static pthread_mutex_t LeakCheckMutex = PTHREAD_MUTEX_INITIALIZER;
static int      LeackCheakInProgress = 0;

struct _SBlockHead
{
    void*                 m_Next;
    int                   m_FreeCount;
}SBlockHead;

typedef struct _SElementHead
{
    int                   m_IsTaken;
    int                   m_DirectMap;
    size_t                m_Size;
    int                   m_ThreadId;
    struct _SElementHead* m_pNext;
    struct _SElementHead* m_pMasterEH;
}SElementHead;

typedef struct _SControlBlock
{
    unsigned char*  m_ListStart;
    SElementHead*   m_pHeadEl;
    SElementHead*   m_pTailEl;
    pthread_mutex_t m_Mutex;
}SControlBlock;

typedef struct _SControl
{
    int              m_Size;
    SControlBlock*   m_pControlBlock; /* Should point to an array of MaxThreadNum elements */
}SControl;

typedef int* pint;

static pint*     pStat = NULL;
static SControl* Control = NULL;
static int       SizesNum = -1;

static int      Initialized = 0;
static long     PageSize    = -1;

#ifndef AIX
static __thread int ThreadInx = -1;
#endif

static void  Init();
static void* DumperThreadEntry(void *par);
static unsigned char* StartList(int ElDataSize, int ThreadId);
static void  SetNewTail(int SizeInx, int ThreadID, SElementHead* pFirstNewEl);
static void* BigMalloc(size_t bytes);
static void  BigFree(void* p);
static int*  PrepareStatFile(int ThreadId, int DumpNum);
static void  PrintErrnoMsg(char* pUserMsg, int ErrNum, int Line);
static void  PrintState();

void* malloc(size_t bytes)
{
    int ThreadID;
    int i = 0;
    SElementHead*  pEH;
    SControlBlock* pCB;

    if(DebugLevel & PRINT_ENTRIES)
        printf("Entered malloc\n");
    IS_INITIALIZED;

    if(bytes <= 0)
    {
        /* Add a meaningful message */
        if(bytes < 0)
            PrintErrnoMsg("malloc() requested to allocate a negative number of bytes", EMPTY_ERROR, __LINE__);
        return NULL;
    }

    /* Find required control block */
    for(i = 0; (Sizes[i] > 0) && (bytes > Sizes[i]); i++);
    if(Sizes[i] < 0)
    {
        return BigMalloc(bytes);
    }

    ThreadID = pthread_self();
    if(ThreadID < 0)
    {
        PrintErrnoMsg("pthread_self() failed", EMPTY_ERROR, __LINE__);
        return NULL;
    }

    if(MakeStatistics)
    {
        if(NULL == pStat[ThreadID])
        {
            if(!(pStat[ThreadID] = PrepareStatFile(ThreadID, 0)))
            {
                PrintErrnoMsg("Abort", EMPTY_ERROR, __LINE__);
                exit(1);
            }
        }
        else
        {
            if(bytes < MAX_TRACED_SIZE){pStat[ThreadID][bytes]++;}
        }
    }

    /* TODO find a better way of handling big thread numbers */
    ThreadID %= MaxThreadNum;

    pCB = (&Control[i].m_pControlBlock[ThreadID]);
    if(NULL == pCB->m_pHeadEl)
    { /* No blocks are created so far. Add the first one */
        pCB->m_ListStart = StartList(Sizes[i], ThreadID);
        if(NULL == pCB->m_ListStart)
        {
            PrintErrnoMsg("Can't create a first block ", EMPTY_ERROR, __LINE__);
            return NULL;
        }
        pCB->m_pHeadEl = (SElementHead*)(pCB->m_ListStart + BLOCK_HEAD_SIZE);
        pCB->m_pTailEl = pCB->m_pHeadEl;
        for(; NULL != pCB->m_pTailEl->m_pNext; pCB->m_pTailEl = pCB->m_pTailEl->m_pNext)
            ;
    }
    else
    { /* No free space in the blocks. Add a new one */
        if(pCB->m_pTailEl == pCB->m_pHeadEl->m_pNext)
        {
            unsigned char* pStart, *p;

            /* Everything is taken */
            /* Let's create a new block */
            pStart = StartList(Sizes[i], ThreadID);
            if(NULL == pStart)
            {  /* TODO - print meaningful message */
                PrintErrnoMsg("Can't create a block ", EMPTY_ERROR, __LINE__);
                return NULL;
            }

            /* Append it to the list */
            for(p = pCB->m_ListStart; NULL != *((void**)p); p = (unsigned char*)(*((void**)p)));
            *((void**)p) = pStart;
            SetNewTail(i, ThreadID, (SElementHead*)(pStart + BLOCK_HEAD_SIZE));
        }
    }
    /* Take the first element */
    pEH = pCB->m_pHeadEl;
    pCB->m_pHeadEl = pCB->m_pHeadEl->m_pNext;
    pEH->m_IsTaken   = 1;
    pEH->m_Size      = bytes;
    pEH->m_DirectMap = 0;
    pEH->m_pNext     = NULL;

    if((LeakCheckStartTime >= 0) && (!LeackCheakInProgress) && (LeakCheckStartTime < gethrtime()))
    {
        if(0 == pthread_mutex_lock(&LeakCheckMutex))
        {
            LeackCheakInProgress = 1;

            if(gethrtime() >= LeakCheckStopTime)
            {
                UnwindEnd();
                LeakCheckStartTime = -1;
            }
            else
            {
                Unwind((void*)(((unsigned char*)pEH) + ELEMENT_HEAD_SIZE), bytes);
            }
            pthread_mutex_unlock(&LeakCheckMutex);

            LeackCheakInProgress = 0;
        }
    }

    return (void*)(((unsigned char*)pEH) + ELEMENT_HEAD_SIZE);
}

void free(void* p)
{
    SElementHead* pEH;
    int           SizeInx;

    if(NULL == p)
    {
        return;
    }

    if((LeakCheckStartTime >= 0) && (!LeackCheakInProgress) && (LeakCheckStartTime < gethrtime()))
    {
        if(0 == pthread_mutex_lock(&LeakCheckMutex))
        {
            LeackCheakInProgress = 1;

            if(gethrtime() >= LeakCheckStopTime)
            {
                UnwindEnd();
                LeakCheckStartTime = -1;
            }
            else
            {
                Unwind(p, -1);
            }
            pthread_mutex_unlock(&LeakCheckMutex);

            LeackCheakInProgress = 0;
        }
    }

    IS_INITIALIZED;

    pEH = (SElementHead*)((unsigned char*)p - ELEMENT_HEAD_SIZE);

    if(1 == pEH->m_DirectMap)
    {
        BigFree(p);
        return;
    }
    if((0 == pEH->m_IsTaken) && (!DoubleFree))
    { /* Double free aborts */
        PrintErrnoMsg("Double free ", EMPTY_ERROR, __LINE__);
        abort();
    }
    pEH->m_IsTaken = 0;
    if(NULL != pEH->m_pMasterEH)
    {
        pEH = pEH->m_pMasterEH;

        pEH->m_IsTaken = 0;
    }

    for(SizeInx = 0; (Sizes[SizeInx] > 0) && (pEH->m_Size > Sizes[SizeInx]); SizeInx++);
    if(Sizes[SizeInx] <= 0)
    {
        PrintErrnoMsg("Unexpected free() error ", EMPTY_ERROR, __LINE__);
        return;
    }

    if(pthread_self() == pEH->m_ThreadId)
    {/* Push back the released element into the free list.
        No need for lock as this is the same thread as the allocator
     */
        pEH->m_pNext = Control[SizeInx].m_pControlBlock[pEH->m_ThreadId].m_pHeadEl;
        Control[SizeInx].m_pControlBlock[pEH->m_ThreadId].m_pHeadEl = pEH;
    }
    else
    {/* Some other thread freed the memory. Have to lock */
        SetNewTail(SizeInx, pEH->m_ThreadId, pEH);
    }
}

void *realloc(void *ptr, size_t size)
{
    SElementHead* pEH;

    if(NULL == ptr)
        return malloc(size);
    if(0 == size )
    {
        free(ptr);
        return NULL;
    }

    pEH = (SElementHead*)((unsigned char*)ptr - ELEMENT_HEAD_SIZE);

    if(pEH->m_Size <= size)
    {
        void* pNew;

        pNew = malloc(size);
        if(NULL == pNew)
            return NULL;
        memcpy(pNew, ptr, pEH->m_Size);
        free(ptr);

        return pNew;
    }
    else
        return ptr;
}

void* calloc(size_t nelem, size_t elsize)
{
    size_t size;
    void*  p;

    size = nelem * elsize;
    p = malloc(size);
    if(NULL != p)
        memset(p, 0, size);
    return p;
}

static void* MallocAndAlign(size_t size, size_t alignment)
{
    int            GuaranteedSize;
    void*          p;
    unsigned char* pTmp;
    void*          pAligned;
    SElementHead*  pEH;

    if(alignment < ALIGNMENT)
        alignment = ALIGNMENT;

    /* Calculate a new size which suffice for the data and alignment */
    GuaranteedSize = size + (ELEMENT_HEAD_SIZE / alignment + 2) * alignment;

    /* Allocate that guaranteed space */
    p = malloc(GuaranteedSize);
    if(NULL == p)
    {
        /* TODO - print a meaningful message */
        return NULL;
    }

    /* Align the data area */
    pTmp = (unsigned char*)p;
    pTmp += ELEMENT_HEAD_SIZE;
    pAligned = (void*)(((unsigned long)pTmp / alignment + 1) * alignment);
    if(DebugLevel & PRINT_ASSERTS)
    {
        if((unsigned char*)p - (unsigned char*)pAligned + GuaranteedSize <= size)
        {
            printf("MallocAndAlign error: not enough room in allocated space for size=%d and alignment=%d\n",
                   size, alignment
                   );
            return NULL;
        }
    }

    /* Register the taken block */
    pEH = (SElementHead*)((unsigned char*) pAligned - ELEMENT_HEAD_SIZE);

    pEH->m_IsTaken   = 1;
    pEH->m_Size      = size;
    pEH->m_pMasterEH = (SElementHead*)((unsigned char*)p - ELEMENT_HEAD_SIZE);
    pEH->m_DirectMap = pEH->m_pMasterEH->m_DirectMap;
    pEH->m_pMasterEH->m_Size = GuaranteedSize;

    return pAligned;
}

void* valloc(size_t elsize)
{
    IS_INITIALIZED;

    return MallocAndAlign(elsize, PageSize);
}

void* memalign(size_t alignment, size_t size)
{
    size_t i;

    for(i = alignment; (i > 1) && (i % 2 == 0); i /= 2);
    if(1 != i)
        return NULL;

    IS_INITIALIZED;

    return MallocAndAlign(size, alignment);
}

#ifdef SOLARIS
#include <mtmalloc.h>
#endif

void mallocctl(int cmd, long value)
{
    switch (cmd) {

    case MTDEBUGPATTERN:
        /*  TODO: Implement it */
        break;
    case MTDOUBLEFREE:
    case MTINITBUFFER:
        if(value)
            DoubleFree = 1;
        else
            DoubleFree = 0;
        break;
    case MTCHUNKSIZE:
        /* Do nothing. Another algoritm is used */
        break;
    default:
        break;
    }
}

#ifdef AIX
/* export LIBPATH=`pwd`:$LIBPATH */
/* -qcpluscmt -L. -L/usr/lib -lpthreads -lc */
int mallopt(int Command, int Value)
{
    /* Description
       The mallopt subroutine is provided for source-level compatibility with the System V malloc
       subroutine. The mallopt subroutine supports the following commands:

       Command     Value    Effect
       M_MXFAST    0        If called before any other malloc
                            subsystem subroutine, this enables
                            the Default allocation policy for
                            the process.
       M_MXFAST    1        If called before any other malloc
                            subsystem subroutine, this enables
                            the 3.1 allocation policy for the process.
       M_DISCLAIM  0        If called while the Default Allocator
                            is enabled, all free memory in
                            the process heap is disclaimed.
       M_MALIGN    N        If called at runtime, sets the default
                            malloc allocation alignment to
                            the value N. The N value must be a
                            power of 2 (greater than or equal to
                            the size of a pointer).

       Parameters
            Command     Specifies the mallopt command to be executed.
            Value       Specifies the size of each element in the array.

       Return Values
            Upon successful completion, mallopt returns 0. Otherwise, 1 is returned.
            If an invalid alignment is requested (one that is not a power of 2),
            mallopt fails with a return value of 1, although subsequent calls to malloc are
            unaffected and continue to provide the alignment value from before the
            failed mallopt call.

       Error Codes
            The mallopt subroutine does not set errno.
    */
    return 0;
}

struct mallinfo mallinfo()
{
    /*
      Description
       The mallinfo subroutine can be used to obtain information about the heap managed by the malloc subsystem.

      Return Values
       The mallinfo subroutine returns a structure of type struct mallinfo, filled in with relevant information and
       statistics about the heap. The contents of this structure can be interpreted using the definition of struct
       mallinfo in /usr/include/malloc.h.

      Error Codes
       The mallinfo subroutine does not set errno.
     */
    struct mallinfo Info;

    return Info;
}

struct mallinfo_heap mallinfo_heap(int Heap)
{
    /*
      Description
       In a multiheap context, the mallinfo_heap subroutine can be used to obtain information about a specific heap
       managed by the malloc subsystem.

      Parameters
       Heap     Specifies which heap to query.

      Return Values
       mallinfo_heap returns a structure of type struct mallinfo_heap, filled in with relevant information and
       statistics about the heap. The contents of this structure can be interpreted using the definition of
       struct mallinfo_heap in /usr/include/malloc.h.

      Error Codes
       The mallinfo_heap subroutine does not set errno.
    */
    struct mallinfo_heap Info;

    return Info;
}

int posix_memalign(void **Pointer2Pointer, size_t Align, size_t Size)
{
    size_t i;

    for(i = Align; (i > 1) && (i % 2 == 0); i /= 2);
    if(1 != i)
        return EINVAL;

    IS_INITIALIZED;

    *Pointer2Pointer = MallocAndAlign(Size, Align);
    if(NULL == *Pointer2Pointer)
        return ENOMEM;
    return 0;
}

#endif /* AIX */

static void Init()
{
    static pthread_mutex_t mutex= PTHREAD_MUTEX_INITIALIZER;
    int                    Status;
    int                    i;
    unsigned char*         p;

    if(DebugLevel & PRINT_ENTRIES)
        printf("Entered Init\n");
    if(1 == Initialized)
        return;

    Status = pthread_mutex_lock(&mutex);
    if(0 != Status)
    {
        PrintErrnoMsg("Init() can't lock the mutex ", Status, __LINE__);
        exit(1);
    }

    /* There may be problems if the application changes configuration data at run time.
       We copy them to avoid such situations.
    */
    MaxThreadNum = yamm_max_thread_num;
    MakeStatistics = yamm_make_statistics;
    DebugLevel = yamm_debug_level;
    DoubleFree = yamm_double_free;
    IsDumperOn = yamm_is_dumper_on;
    DumpInterval = yamm_dump_interval;
    LeakCheckStartTime = yamm_leak_check_start_time;
    LeakCheckStopTime = yamm_leak_check_stop_time ;

    for(SizesNum = 0; yamm_sizes[SizesNum] >= 0; SizesNum++);
    SizesNum++;

    /* Allocate memory for the main control structures */
    p = (unsigned char *)
        mmap((void*)0, sizeof(SControl) * SizesNum +
                       sizeof(SControlBlock) * SizesNum * MaxThreadNum +
                       sizeof(pint) * MaxThreadNum +
                       sizeof(int) * SizesNum,
             PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, (off_t)0
            );
    if(MAP_FAILED == p)
    {
        PrintErrnoMsg("Init() can't allocate memory for the main control structures ", errno, __LINE__);
        abort();
    }
    Control = (SControl*)p;
    pStat = (pint*)(p + (sizeof(SControl) * SizesNum + sizeof(SControlBlock) * SizesNum * MaxThreadNum));
    Sizes = (pint)(p + (sizeof(SControl) * SizesNum + sizeof(SControlBlock) * SizesNum * MaxThreadNum + sizeof(pint) * MaxThreadNum));

    /* Initializae the control data */
    for(i = 0; i < SizesNum; i++)
        Sizes[i] = yamm_sizes[i];

    PageSize = sysconf(_SC_PAGESIZE);
    for(i = 0; i < SizesNum; i++)
    {
        int n;

        Control[i].m_Size = Sizes[i];
        Control[i].m_pControlBlock =
            (SControlBlock*)(p + (sizeof(SControl) * SizesNum + sizeof(SControlBlock) * i * MaxThreadNum));
        for(n = 0; n < MaxThreadNum; n++)
        {
            Control[i].m_pControlBlock[n].m_ListStart = NULL;
            Control[i].m_pControlBlock[n].m_pHeadEl   = NULL;
            Control[i].m_pControlBlock[n].m_pTailEl   = NULL;
            Status = pthread_mutex_init(&(Control[i].m_pControlBlock[n].m_Mutex), NULL);
            if(0 != Status)
            {
                PrintErrnoMsg("Init() can't initialize a mutex ", Status, __LINE__);
                exit(1);
            }
        }
    }
    for(i = 0; i < MaxThreadNum; i++)
        pStat[i] = NULL;

    Initialized = 1;

    if(IsDumperOn)
    {
        /* start dumper thread */
        pthread_t DumperThreadId;
        pthread_create(&DumperThreadId, NULL, DumperThreadEntry, NULL);
    }

    if(LeakCheckStartTime >= 0)
    {
        LeakCheckStartTime = gethrtime() + LeakCheckStartTime * 1000000000;
        LeakCheckStopTime  = gethrtime() + LeakCheckStopTime  * 1000000000;
        LeackCheakInProgress = 1;
        UnwindIni();
        LeackCheakInProgress = 0;
    }

    Status = pthread_mutex_unlock(&mutex);
    if(0 != Status)
    {
        PrintErrnoMsg("Init() can't release the mutex ", Status, __LINE__);
        exit(1);
    }
}

static void* DumperThreadEntry(void *par)
{
    pint*     pStatLocal = NULL;
    pint*     pStatPrev  = NULL;
    int       DumpNum = 1;
    int       i;

    sleep(2);

    pStatLocal = (pint*)malloc(sizeof(pint) * MaxThreadNum);
    pStatPrev  = (pint*)malloc(sizeof(pint) * MaxThreadNum);
    if((NULL == pStatPrev) || (NULL == pStatLocal))
        return NULL;

    do
    {
        sleep(DumpInterval - 1);
        /* Reinit the structures */
        for(i = 0; i < MaxThreadNum; i++)
        {
            /* Create a new file if the previous file exists and there were allocations */
            if(NULL == pStat[i])
                pStatLocal[i] = NULL;
            else
            {
                pint p, pEnd;
                p = pStat[i];
                pEnd = p + MAX_TRACED_SIZE;
                for(; p < pEnd; p++)
                    if(*p)
                        break;
                if(p < pEnd)
                    pStatLocal[i] = PrepareStatFile(i, DumpNum);
                else
                    pStatLocal[i] = NULL;
            }
        }

        /* Save old pointers */
        memcpy(pStatPrev, pStat, sizeof(pint) * MaxThreadNum);

        /* Set new pointers */
        memcpy(pStat, pStatLocal, sizeof(pint) * MaxThreadNum);

        sleep(1);
        /* Release old pointers */
        for(i = 0; i < MaxThreadNum; i++)
            if(NULL != pStatPrev[i])
                munmap(pStatPrev[i], MAX_TRACED_SIZE * sizeof(int));
        DumpNum++;
    }while(1);
    return NULL;
}

static unsigned char* StartList(int ElDataSize, int ThreadId)
{
    void*          p;
    unsigned char* pStart;
    SElementHead*  pEH = NULL;
    int            k;
    int            ElementsPerBlock;

    if(DebugLevel & PRINT_ENTRIES)
        printf("Entered StartList\n");
    p = mmap((void*)0, BLOCK_SIZE(ElDataSize), PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, (off_t)0);

    if(MAP_FAILED == p)
    {
        PrintErrnoMsg("StartList() can't map memory ", errno, __LINE__);
        return NULL;
    }

    memset(p, 0, BLOCK_SIZE(ElDataSize)); /* Cleanup the area */
    *((void**)p) = NULL; /* There is no next block */

    /* Initialize all block elements */
    pStart = ((unsigned char*)p) + BLOCK_HEAD_SIZE;
    ElementsPerBlock = ELEMENTS_PER_BLOCK(ElDataSize);
    for(k = 0; k < ElementsPerBlock; k++)
    {
        pEH = (SElementHead*)(pStart + ELEMENT_SIZE(ElDataSize) * k);
        pEH->m_IsTaken   = 0;
        pEH->m_DirectMap = 0;
        pEH->m_Size      = ElDataSize;
        pEH->m_ThreadId  = ThreadId;
        pEH->m_pNext     = (SElementHead*)(pStart + ELEMENT_SIZE(ElDataSize) * (k + 1));
        pEH->m_pMasterEH = NULL;
    }
    pEH->m_pNext = NULL;

    return (unsigned char *)p;
}

static void SetNewTail(int SizeInx, int ThreadID, SElementHead* pFirstNewEl)
{
    int            Status;
    SControlBlock* pCB;

    pCB = &(Control[SizeInx].m_pControlBlock[ThreadID]);
    Status = pthread_mutex_lock(&(pCB->m_Mutex));
    if(0 != Status)
    {
        PrintErrnoMsg("SetNewTail() can't lock a mutex ", Status, __LINE__);
        exit(1);
    }

    /* pFirstNewEl is never equal to NULL */
    pCB->m_pTailEl->m_pNext = pFirstNewEl;
    for(; NULL != pCB->m_pTailEl->m_pNext; pCB->m_pTailEl = pCB->m_pTailEl->m_pNext)
        ;

    Status = pthread_mutex_unlock(&(pCB->m_Mutex));
    if(0 != Status)
    {
        PrintErrnoMsg("SetNewTail() can't release a mutex ", Status, __LINE__);
        exit(1);
    }
}

static void* BigMalloc(size_t bytes)
{
    void*         p;
    SElementHead* pEH;

    if(DebugLevel & PRINT_ENTRIES)
        printf("Entered BigMalloc\n");

    if(MakeStatistics)
    {
        int ThreadID;

        ThreadID = pthread_self();
        if(NULL == pStat[ThreadID])
        {
            if(!(pStat[ThreadID] = PrepareStatFile(ThreadID, 0)))
            {
                PrintErrnoMsg("Abort", EMPTY_ERROR, __LINE__);
                exit(1);
            }
        }
        else
        {
            pStat[ThreadID][0]++;
        }
    }

    p = mmap((void*)0, ELEMENT_HEAD_SIZE + bytes, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, (off_t)0);
    if(MAP_FAILED == p)
    {
        /* TODO check correspondence of error codes */
        PrintErrnoMsg("BigMalloc() can't map memory ", errno, __LINE__);
        return NULL;
    }
    pEH = (SElementHead*)p;

    pEH->m_IsTaken   = 1;
    pEH->m_Size      = bytes;
    pEH->m_DirectMap = 1;
    pEH->m_pMasterEH = NULL;
    return (((unsigned char*)p) + ELEMENT_HEAD_SIZE);
}

static void BigFree(void* p)
{
    SElementHead* pEH;

    if(DebugLevel & PRINT_ENTRIES)
        printf("Entered BigFree\n");

    if(NULL == p)
    {
        return;
    }

    pEH = (SElementHead*)((unsigned char*)p - ELEMENT_HEAD_SIZE);

    if(NULL != pEH->m_pMasterEH)
        pEH = pEH->m_pMasterEH;

     if(-1 == munmap((void*)pEH, pEH->m_Size + ELEMENT_HEAD_SIZE))
     {
        /* TODO check correspondence of error codes */
        PrintErrnoMsg("BigFree() can't unmap memory ", errno, __LINE__);
        return;
    }
}

static int* PrepareStatFile(int ThreadId, int DumpNum)
{
    char FileName[80];
    int  fd;
    int* p;

    sprintf(FileName, "yamm_stat_%07d_%07d_%03d.dat", getpid(), ThreadId, DumpNum);
    fd = open(FileName, O_RDWR | O_CREAT, 0644);
    if(fd < 0)
    {
        PrintErrnoMsg("PrepareStatFile() can't create/open a stat file ", errno, __LINE__);
        return NULL;
    }

    /* Set the required file size */
    if(lseek(fd, (off_t)(MAX_TRACED_SIZE * sizeof(int) - 1), SEEK_SET) < 0)
    {
        PrintErrnoMsg("PrepareStatFile() lseek problem ", errno, __LINE__);
        return NULL;
    }
    if(1 != write(fd, "", 1))
    {
        PrintErrnoMsg("PrepareStatFile() can't set the file size ", errno, __LINE__);
        return NULL;
    }

    /* Reopen the file to flush buffers and make everything clean */
    if(-1 == close(fd))
    {
        PrintErrnoMsg("PrepareStatFile() reopen (close) problem ", errno, __LINE__);
        return NULL;
    }
    fd = open(FileName, O_RDWR, 0644);
    if(fd < 0)
    {
        PrintErrnoMsg("PrepareStatFile() reopen problem ", errno, __LINE__);
        return NULL;
    }

    /* Map the file */
    p = (int*)mmap((void*)0, MAX_TRACED_SIZE * sizeof(int), PROT_READ|PROT_WRITE, MAP_ALIGN|MAP_SHARED, fd, (off_t)0);
    if(MAP_FAILED == p)
    {
        PrintErrnoMsg("PrepareStatFile() can't map stat file ", errno, __LINE__);
        return NULL;
    }

    close(fd);

    /* Reset the counters */
    memset((void*)p, 0, MAX_TRACED_SIZE * sizeof(int));

    /* It's ready now */
    return p;
}

static void PrintErrnoMsg(char* pUserMsg, int ErrNum, int Line)
{
    static struct
    {
        char* Name;
        int   Code;
    }Errors[] =
        {
            { "E2BIG"             , E2BIG},
            { "EACCES"            , EACCES},
            { "EADDRINUSE"        , EADDRINUSE},
            { "EADDRNOTAVAIL"     , EADDRNOTAVAIL},
            { "EADV"              , EADV},
            { "EAFNOSUPPORT"      , EAFNOSUPPORT},
            { "EAGAIN"            , EAGAIN},
            { "EALREADY"          , EALREADY},
            { "EBADE"             , EBADE},
            { "EBADF"             , EBADF},
            { "EBADFD"            , EBADFD},
            { "EBADMSG"           , EBADMSG},
            { "EBADR"             , EBADR},
            { "EBADRQC"           , EBADRQC},
            { "EBADSLT"           , EBADSLT},
            { "EBFONT"            , EBFONT},
            { "EBUSY"             , EBUSY},
            { "ECANCELED"         , ECANCELED},
            { "ECHILD"            , ECHILD},
            { "ECHRNG"            , ECHRNG},
            { "ECOMM"             , ECOMM},
            { "ECONNABORTED"      , ECONNABORTED},
            { "ECONNREFUSED"      , ECONNREFUSED},
            { "ECONNRESET"        , ECONNRESET},
            { "EDEADLK"           , EDEADLK},
            { "EDEADLOCK"         , EDEADLOCK},
            { "EDESTADDRREQ"      , EDESTADDRREQ},
            { "EDOM"              , EDOM},
            { "EDQUOT"            , EDQUOT},
            { "EEXIST"            , EEXIST},
            { "EFAULT"            , EFAULT},
            { "EFBIG"             , EFBIG},
            { "EHOSTDOWN"         , EHOSTDOWN},
            { "EHOSTUNREACH"      , EHOSTUNREACH},
            { "EIDRM"             , EIDRM},
            { "EILSEQ"            , EILSEQ},
            { "EINPROGRESS"       , EINPROGRESS},
            { "EINTR"             , EINTR},
            { "EINVAL"            , EINVAL},
            { "EL3RST"            , EL3RST},
            { "ELIBACC"           , ELIBACC},
            { "ELIBBAD"           , ELIBBAD},
            { "ELIBEXEC"          , ELIBEXEC},
            { "ELIBMAX"           , ELIBMAX},
            { "ELIBSCN"           , ELIBSCN},
            { "ELNRNG"            , ELNRNG},
            { "ELOCKUNMAPPED"     , ELOCKUNMAPPED},
            { "ELOOP"             , ELOOP},
            { "EMFILE"            , EMFILE},
            { "EMLINK"            , EMLINK},
            { "EMSGSIZE"          , EMSGSIZE},
            { "EMULTIHOP"         , EMULTIHOP},
            { "ENAMETOOLONG"      , ENAMETOOLONG},
            { "ENETDOWN"          , ENETDOWN},
            { "ENETRESET"         , ENETRESET},
            { "ENETUNREACH"       , ENETUNREACH},
            { "ENFILE"            , ENFILE},
            { "ENOANO"            , ENOANO},
            { "ENOBUFS"           , ENOBUFS},
            { "ENOCSI"            , ENOCSI},
            { "ENODATA"           , ENODATA},
            { "ENODEV"            , ENODEV},
            { "ENOENT"            , ENOENT},
            { "ENOEXEC"           , ENOEXEC},
            { "ENOLCK"            , ENOLCK},
            { "ENOLINK"           , ENOLINK},
            { "ENOMEM"            , ENOMEM},
            { "ENOMSG"            , ENOMSG},
            { "ENONET"            , ENONET},
            { "ENOPKG"            , ENOPKG},
            { "ENOPROTOOPT"       , ENOPROTOOPT},
            { "ENOSPC"            , ENOSPC},
            { "ENOSR"             , ENOSR},
            { "ENOSTR"            , ENOSTR},
            { "ENOSYS"            , ENOSYS},
            { "ENOTACTIVE"        , ENOTACTIVE},
            { "ENOTBLK"           , ENOTBLK},
            { "ENOTCONN"          , ENOTCONN},
            { "ENOTDIR"           , ENOTDIR},
            { "ENOTUNIQ"          , ENOTUNIQ},
            { "ENXIO"             , ENXIO},
            { "EOPNOTSUPP"        , EOPNOTSUPP},
            { "EOVERFLOW"         , EOVERFLOW},
            { "EOWNERDEAD"        , EOWNERDEAD},
            { "EPERM"             , EPERM},
            { "EPFNOSUPPORT"      , EPFNOSUPPORT},
            { "EPIPE"             , EPIPE},
            { "EPROTO"            , EPROTO},
            { "EPROTONOSUPPORT"   , EPROTONOSUPPORT},
            { "EPROTOTYPE"        , EPROTOTYPE},
            { "ERANGE"            , ERANGE},
            { "EREMCHG"           , EREMCHG},
            { "EREMOTE"           , EREMOTE},
            { "ERESTART"          , ERESTART},
            { "EROFS"             , EROFS},
            { "ESHUTDOWN"         , ESHUTDOWN},
            { "ESOCKTNOSUPPORT"   , ESOCKTNOSUPPORT},
            { "ESPIPE"            , ESPIPE},
            { "ESRCH"             , ESRCH},
            { "ESRMNT"            , ESRMNT},
            { "ESTALE"            , ESTALE},
            { "ESTRPIPE"          , ESTRPIPE},
            { "ETIME"             , ETIME},
            { "ETIMEDOUT"         , ETIMEDOUT},
            { "ETOOMANYREFS"      , ETOOMANYREFS},
            { "ETXTBSY"           , ETXTBSY},
            { "EUNATCH"           , EUNATCH},
            { "EUSERS"            , EUSERS},
            { "EWOULDBLOCK"       , EWOULDBLOCK},
            { "EXDEV"             , EXDEV},
            { "EXFULL"            , EXFULL},
            { "OK"                , 0},
            { ""                  , EMPTY_ERROR},
            { "UNKNOWN CODE"      , STOP_ERROR_LOOKUP}
        };

    char Message[512];
    int  i;

    for(i = 5; i >= 0; i--)
    {
        Message[i] = (char)(Line % 10) + '0';
        Line /= 10;
    }
    Message[6] = ':'; Message[7] = '\0';
    strcat(Message, pUserMsg);

    /* We can't use strerror_r() here as it calls malloc() on some OSs */
    for(i = 0; (abs(Errors[i].Code) != STOP_ERROR_LOOKUP) && (abs(Errors[i].Code) != abs(ErrNum)); i++);
    strcat(Message, Errors[i].Name);
    strcat(Message, "\n");

    write(STDERR_FILENO, Message, strlen(Message));
}

static void PrintState()
{
    int i;

    for(i = 0; i < SizesNum; i++)
    {
        int k;
        for(k = 0; k < MaxThreadNum; k++)
        {
            void* p;

            if(NULL == Control[i].m_pControlBlock[k].m_ListStart)
                continue;
            printf("Size: %4d  Thread: %2d  ", Control[i].m_Size, k);

            for(p = Control[i].m_pControlBlock[k].m_ListStart; NULL != p; p = *((void**)p))
            {
                int   n;
                unsigned char* ptr;

                for(n =0; n < ELEMENTS_PER_BLOCK(Control[i].m_Size); n++)
                {
                    SElementHead* pEH;

                    ptr = (unsigned char*)p + (BLOCK_HEAD_SIZE + n * ELEMENT_SIZE(Control[i].m_Size));
                    pEH = (SElementHead*)ptr;
                    if(Control[i].m_pControlBlock[k].m_pHeadEl == pEH)
                        printf("H ");
                    else
                    {
                        if(Control[i].m_pControlBlock[k].m_pTailEl == pEH)
                            printf("T ");
                        else
                        {
                            if(pEH->m_IsTaken)
                                printf("* ");
                            else
                                printf("  ");
                        }
                    }

                    fflush(stdout);
                }
                printf(" | ");
                fflush(stdout);
            }
            printf("\n");
        }
    }
}
