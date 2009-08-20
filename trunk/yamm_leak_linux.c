#ifdef LINUX
#include <stdlib.h>
#include <stdio.h>
#include <time.h> 
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <execinfo.h>
#include <errno.h>

typedef struct
{
    void* m_p;             /* used as a pointer to allocated block when in BadAlloc array */
                           /* used as a call counter when in GoodAlloc array */
    int   m_Size;          /* allocated size */
    char* m_pCode;         /* pointer to the backtrace string in the file */
    int   m_CodeStringLen; /* length of the backtrace string len */
} SInfoBlock;

typedef unsigned char byte;

#define MAX_STACK_SIZE    50
#define WRITE_BUFFER_SIZE 10240

static  char LeakFileName[] = "yamm_leak.dat";
static  byte WriteBuffer[WRITE_BUFFER_SIZE];
static  int WriteFD;

static void UnwindIni()
{
    WriteFD = creat(LeakFileName, 0644);
    if(-1 == WriteFD)
    {
        const char ErroMsgPrefix[] = "Can't create leak data file ";
        write(2, ErroMsgPrefix, strlen(ErroMsgPrefix));
        write(2, LeakFileName, strlen(LeakFileName));
        write(2, " : ", 3);
        write(2, strerror(errno), strlen(strerror(errno)));
        write(2, "\n", 1);
        return;
    }
}

static void UnwindEnd()
{
    if(-1 == WriteFD)
        return;
    close(WriteFD);
    (void*)gethrtime(); /* Just to help compiler */
}

static void Unwind(char* pData, int size)
{
    void *pTrace[MAX_STACK_SIZE];
    char **ppMessages = (char **)NULL;
    int  i, TraceSize = 0;

    SInfoBlock InfoBlock;
    byte* p;

    if(-1 == WriteFD)
        return;

    /* Get the backtrace */
    TraceSize = backtrace(pTrace, MAX_STACK_SIZE);
    ppMessages = backtrace_symbols(pTrace, TraceSize);
    if(NULL == ppMessages)
        return;

    /* Write the leak prefix */
    InfoBlock.m_p    = pData;
    InfoBlock.m_Size = size;
    InfoBlock.m_CodeStringLen = 0;
    for(i = 0; i < TraceSize; i++)
        InfoBlock.m_CodeStringLen += strlen(ppMessages[i]) + 1;
    if(sizeof(InfoBlock) != write(WriteFD, &InfoBlock, sizeof(InfoBlock)))
    {
        static char ErrMsg[] = "Leak data file is corrupted\n";
        write(2, ErrMsg, strlen(ErrMsg));
    }

    /* Write the backtrace */
    p = WriteBuffer;
    *p = '\0';
    for(i = 0; i < TraceSize; i++)
    {
        int Len = strlen(ppMessages[i]) + 1;
        if(Len > WRITE_BUFFER_SIZE - (p - WriteBuffer))
        {
            if((p - WriteBuffer) != write(WriteFD, WriteBuffer, p - WriteBuffer))
            {
                static char ErrMsg[] = "Leak data file is corrupted\n";
                write(2, ErrMsg, strlen(ErrMsg));
            }
            p = WriteBuffer;
            *p = '\0';
        }

        memcpy(p, ppMessages[i], Len);
        p += Len;
    }

    if((p - WriteBuffer) != write(WriteFD, WriteBuffer, p - WriteBuffer))
    {
        static char ErrMsg[] = "Leak data file is corrupted\n";
        write(2, ErrMsg, strlen(ErrMsg));
    }

    free(ppMessages);
}

static unsigned long long gethrtime()
{
    struct timespec TimeSpec;
    clock_gettime(CLOCK_REALTIME, &TimeSpec);
    return (TimeSpec.tv_sec * 1000000000LL + TimeSpec.tv_nsec);
}

/* ANALYZER */

static void CheckFree(SInfoBlock* pInfoBlock);
static void AddAlloc(SInfoBlock* pInfoBlock);
static void Report();
static void PrintSourceCodeInfo(SInfoBlock* pInfoBlock);
static int  GetInfoViaAddr2line(char* pSourceInfo, char* pBinName, char* pAddress);

static int BlockNumExt = 10000;
static int BadBlockNum = 0;
static SInfoBlock* pBadBlock = NULL; /* a member is not used if m_p contains NULL */

static int GoodBlockNum = 0;
static SInfoBlock* pGoodBlock = NULL; /* a member is not used if m_p is negative */

static void ReadAndProcessData()
{
    struct stat State;
    int fd;
    int len;
    byte* pData, *p;

    if((fd = open(LeakFileName, O_RDONLY)) < 0)
    {
        static char ErrorMsgPrefix[] = "Cannot open leak data file ";
        write(2, ErrorMsgPrefix, strlen(ErrorMsgPrefix));
        write(2, LeakFileName, strlen(LeakFileName));
        write(2, " : ", 3);
        write(2, strerror(errno), strlen(strerror(errno)));
        write(2, "\n", 1);
        exit(0);
    }
    if(fstat(fd, &State) < 0)
    {
        static char ErrorMsgPrefix[] = "Cannot obtain leak data file ";
        static char ErrorMsgInsert[] = " details: ";;
        write(2, ErrorMsgPrefix, strlen(ErrorMsgPrefix));
        write(2, LeakFileName, strlen(LeakFileName));
        write(2, ErrorMsgInsert, strlen(ErrorMsgInsert));
        write(2, strerror(errno), strlen(strerror(errno)));
        write(2, "\n", 1);
        exit(0);
    }
    len = State.st_size;
    pData = (byte*)mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
    if(MAP_FAILED == pData)
    {
        static char ErrorMsgPrefix[] = "Cannot map leak data file ";
        write(2, ErrorMsgPrefix, strlen(ErrorMsgPrefix));
        write(2, LeakFileName, strlen(LeakFileName));
        write(2, " : ", 3);
        write(2, strerror(errno), strlen(strerror(errno)));
        write(2, "\n", 1);
        exit(0);
    }
    close(fd);

    for(p = pData; p < (pData + State.st_size); )
    {
        SInfoBlock InfoBlock;

        memcpy(&InfoBlock, p, sizeof(InfoBlock)); p += sizeof(InfoBlock);
        InfoBlock.m_pCode = (char*)p;
        p += InfoBlock.m_CodeStringLen;

        if(InfoBlock.m_Size < 0)
            CheckFree(&InfoBlock);
        else
            AddAlloc(&InfoBlock);
    }

    Report();

    if((NULL != pData) && (MAP_FAILED != pData))
        munmap(pData, len);
}

static void CheckFree(SInfoBlock* pInfoBlock)
{
    int i = 0;
    int j = 0;
    /* Match free and alloc */
    for(i = 0; i < BadBlockNum; i++)
    {
        if(pBadBlock[i].m_p == pInfoBlock->m_p)
        {
            /* Find the relevant slot in GoodBlock */
            for(j = 0; j < GoodBlockNum; j++)
            {
                if((pGoodBlock[j].m_p != (void*)-1) &&
                   (pGoodBlock[j].m_CodeStringLen == pBadBlock[i].m_CodeStringLen) &&
                   (memcmp(pGoodBlock[j].m_pCode, pBadBlock[i].m_pCode, pBadBlock[i].m_CodeStringLen) == 0)
                   )
                {
                    pGoodBlock[j].m_p = (void*)((long)(pGoodBlock[j].m_p) + 1);
                    pBadBlock[i].m_p = NULL;
                    return;
                }
                if(pGoodBlock[j].m_p == (void*)-1)
                {/* The whole set has scanned. Nothing found */
                    memcpy(pGoodBlock + j, pBadBlock + i, sizeof(SInfoBlock));
                    pGoodBlock[j].m_p = (void*)1;
                    pBadBlock[i].m_p = NULL;
                    return;
                }
            }
            if(j >= GoodBlockNum)
            { /* No space for storing new block */
                SInfoBlock* pNewBlock;
                int NewGoodBlockNum;
                int n;
                NewGoodBlockNum = GoodBlockNum + BlockNumExt;
                pNewBlock = (SInfoBlock*)malloc(NewGoodBlockNum * sizeof(SInfoBlock));
                if(NULL == pNewBlock)
                {
                    char Message[] = "Cannot allocate more memory for good blocks\nAborting\n";
                    write(2, Message, strlen(Message));
                    exit(0);
                }
                for(n = 0; n < NewGoodBlockNum; n++)
                    pNewBlock[n].m_p = (void*)-1; /* Initialize the new array */
                if(NULL != pGoodBlock)
                {
                    memcpy(pNewBlock, pGoodBlock, GoodBlockNum * sizeof(SInfoBlock)); /* Save the old information */
                    free(pGoodBlock); /* Release unused memory */
                }
                /* Start using fresh memory */
                pGoodBlock = pNewBlock;
                GoodBlockNum = NewGoodBlockNum;

                /* Store the matched datat */
                memcpy(pGoodBlock + j, pBadBlock + i, sizeof(SInfoBlock));
                pGoodBlock[j].m_p = (void*)1;
                pBadBlock[i].m_p = NULL;
                return;
            }
        }
    }
}

static void AddAlloc(SInfoBlock* pInfoBlock)
{
    int i = 0;
    /* Find an empty slot in BadBlock */
    for(i = 0; (i < BadBlockNum) && (NULL != pBadBlock[i].m_p); i++);
    if(i >= BadBlockNum)
    { /* No space for storing new block */
        SInfoBlock* pNewBlock;
        int NewBadBlockNum;
        int n;
        NewBadBlockNum = BadBlockNum + BlockNumExt;
        pNewBlock = (SInfoBlock*)malloc(NewBadBlockNum * sizeof(SInfoBlock));
        if(NULL == pNewBlock)
        {
            char Message[] = "Cannot allocate more memory for bad blocks\nAborting\n";
            write(2, Message, strlen(Message));
            exit(0);
        }
        for(n = 0; n < NewBadBlockNum; n++)
            pNewBlock[n].m_p = NULL; /* Initialize the new array */
        if(NULL != pBadBlock)
        {
            memcpy(pNewBlock, pBadBlock, BadBlockNum * sizeof(SInfoBlock)); /* Save the old information */
            free(pBadBlock); /* Release unused memory */
        }
        /* Start using fresh memory */
        pBadBlock = pNewBlock;
        BadBlockNum = NewBadBlockNum;
    }

    memcpy(pBadBlock + i, pInfoBlock, sizeof(SInfoBlock));
}

static void Report()
{
    /* For each non-empty BadBlock
       print potential leak if there are good blocks allocated at the backtrace
       print leak otherwise
    */
    int i = 0;
    int j = 0;
    for(i = 0; i < BadBlockNum; i++)
    {
        if(NULL != pBadBlock[i].m_p)
        { /* There is a non-free block */
            int FirstTime = 1;
            for(j = 0; (j < GoodBlockNum) && (1 == FirstTime); j++)
            {
                if((pBadBlock[i].m_CodeStringLen == pGoodBlock[j].m_CodeStringLen) &&
                   (strcmp(pBadBlock[i].m_pCode, pGoodBlock[j].m_pCode) == 0)
                   )
                {/* There were alloc's and free's with this backtrace */
                    printf("Potential memory leak:\n%d bytes allocated at \n", pBadBlock[i].m_Size);
                    PrintSourceCodeInfo(pBadBlock + i);
                    FirstTime = 0;
                }
            }
            if((1 == FirstTime))
            {
                printf("Memory leak:\n%d bytes allocated at \n", pBadBlock[i].m_Size);
                PrintSourceCodeInfo(pBadBlock + i);
            }
        }
    }
}

static void PrintSourceCodeInfo(SInfoBlock* pInfoBlock)
{
#define MAX_FILE_NAME_SIZE 255
    char* p;

    for(p = pInfoBlock->m_pCode;
        p - pInfoBlock->m_pCode < pInfoBlock->m_CodeStringLen;
        p += strlen(p) + 1
       )
    {
        char* pTmp = p;
        char* pAddrStart;
        char  BinName[MAX_FILE_NAME_SIZE];
        char  Address[MAX_FILE_NAME_SIZE];

        /* Split the line and keep only binary name and address */
        for(; ('\0' != *pTmp) && (' ' != *pTmp ) && ('(' != *pTmp); pTmp++);
        if('\0' != *pTmp)
        {
            int Len = pTmp - p > (MAX_FILE_NAME_SIZE - 1) ? MAX_FILE_NAME_SIZE - 1 : pTmp - p;
            memcpy(BinName, pTmp - Len, Len);
            BinName[Len] = '\0';
        }

        /* Skip the rest until '[' marking the address start*/
        for(; ('\0' != *pTmp) && ('[' != *pTmp); pTmp++);
        if('[' != *pTmp)
        {
            printf("%s   : Cannot provide more information\n", p);
            continue;
        }
        /* Get the address */
        pAddrStart = ++pTmp;
        /* Find the ']' */
        for(; ('\0' != *pTmp) && (']' != *pTmp); pTmp++);
        if(']' != *pTmp)
        {
            printf("%s   : Cannot provide more information\n", p);
            continue;
        }

        memcpy(Address, pAddrStart, pTmp - pAddrStart);
        Address[pTmp - pAddrStart] = '\0';

        if(GetInfoViaAddr2line(BinName, BinName, Address))
            printf("%s", BinName);
        else
            printf("%s   : Cannot provide more information\n", p);
    }
/*printf("#%04d %s: %d\n", N++, FileName, RealFileNameLen);*/
    printf("\n");
}

static int  GetInfoViaAddr2line(char* pSourceInfo, char* pBinName, char* pAddress)
{
    static char Arg0[] = "/usr/bin/addr2line";
    static char Arg1[] = "-iC";
    static char Arg2[] = "-e";

    int   fd[2];
    int   Len;
    pid_t ChildPID;

    pipe(fd);
    /* Create the child process for addr2line */
    if(-1 == (ChildPID = fork())) //create child process
    {
        perror("Could call addr2line");
        return 0;
    }

    if(0 == ChildPID)
    {
        close(fd[0]);
        if(-1 == execl(Arg0, Arg0, Arg1, Arg2, pBinName, pAddress, (char *) NULL))
        {
            perror(NULL);
            return 0;
        }
        return 1;
    /*    printf("/usr/bin/addr2line -iC -e % %s\n", BinName, Address);*/
    }
    else
    {
        close(fd[1]);
        Len = read(fd[0], pSourceInfo, MAX_FILE_NAME_SIZE - 1);
        pSourceInfo[Len] = '\0';
        return 1;
    }
}
#endif
