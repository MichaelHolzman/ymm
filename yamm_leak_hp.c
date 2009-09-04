#ifdef HPUX

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <uwx.h>
#include <uwx_self.h>

typedef struct
{
    void* m_p;             /* used as a pointer to allocated block when in BadAlloc array */
                           /* used as a call counter when in GoodAlloc array */
    int   m_Size;          /* allocated size */
    char* m_pCode;         /* pointer to the backtrace string in the file */
    int   m_CodeStringLen; /* length of the backtrace string len */
} SInfoBlock;

static struct uwx_env *pEnv;
static struct uwx_self_info *pInfo;

typedef unsigned char byte;

#define WRITE_BUFFER_SIZE 10240

static byte WriteBuffer[WRITE_BUFFER_SIZE];
static int  LibUnwindIsReady = 0;

static void UnwindIni()
{
    pEnv = uwx_init();
    pInfo = uwx_self_init_info(pEnv);
    if(UWX_OK == uwx_register_callbacks(pEnv, (intptr_t)pInfo, uwx_self_copyin, uwx_self_lookupip))
        LibUnwindIsReady = 1;
    else
        write(2, "libunwind is not initialized properly\n", 39);
}

static void UnwindEnd()
{
    uwx_self_free_info(pInfo);
    uwx_free(pEnv);
}

static void Unwind(char* pData, int size, int fd)
{
    int   Level = 0;
    int   Status;
    char *pInlineFunc = NULL;
    char *pSourceFile = NULL;
    int   LineNumber;
    int   InlineContext;

    SInfoBlock InfoBlock;
    int   StringInfoLen;
    byte* p;

    if((-1 == fd) || (LibUnwindIsReady != 1))
        return;

    InfoBlock.m_p    = pData;
    InfoBlock.m_Size = size;

    Status = uwx_self_init_context(pEnv);

    InlineContext = 0;
    InfoBlock.m_CodeStringLen = 0;
    p = WriteBuffer;
    while(1)
    {
        if(0 != InlineContext)
            Status = uwx_step_inline(pEnv);
        else
            Status = uwx_step(pEnv);
        if (Status != UWX_OK)
            break;

        Status = uwx_get_source_info(pEnv, &pInlineFunc, &pSourceFile, &LineNumber, &InlineContext);
        if(UWX_OK == Status)
        {
            if(pSourceFile != NULL)
            {
                StringInfoLen = strlen(pSourceFile);
                if(WRITE_BUFFER_SIZE - (p - WriteBuffer) - sizeof(int) - StringInfoLen - 1 > 0)
                {
                    memcpy(p, &StringInfoLen, sizeof(int)); p += sizeof(int);
                    memcpy(p, pSourceFile, StringInfoLen);  p += StringInfoLen;
                    memcpy(p, &LineNumber, sizeof(int));    p += sizeof(int);
                    InfoBlock.m_CodeStringLen += 2 * sizeof(int) + StringInfoLen;
                }
            }
        }
    }
    if((sizeof(InfoBlock) != write(fd, &InfoBlock, sizeof(InfoBlock))) ||
       (InfoBlock.m_CodeStringLen != write(fd, WriteBuffer, InfoBlock.m_CodeStringLen))
      )
    {
        static char ErrMsg[] = "Leak data file is corrupted\n";
        write(2, ErrMsg, strlen(ErrMsg));
    }
}

/* ANALYZER */

static void CheckFree(SInfoBlock* pInfoBlock);
static void CheckPoison(SInfoBlock* pInfoBlock);
static void AddAlloc(SInfoBlock* pInfoBlock);
static void Report();
static void PrintSourceCodeInfo(SInfoBlock* pInfoBlock);

static int BlockNumExt = 10000;
static int BadBlockNum = 0;
static SInfoBlock* pBadBlock = NULL; /* a member is not used if m_p contains NULL */

static int GoodBlockNum = 0;
static SInfoBlock* pGoodBlock = NULL; /* a member is not used if m_p is negative */

#define LEAK_MODE 0
#define POISON_MODE 1

static void ReadAndProcessData(int Mode, char* pFileName)
{
    struct stat State;
    int fd;
    int len;
    byte* pData, *p;

    if((fd = open(pFileName, O_RDONLY)) < 0)
    {
        static char ErrorMsgPrefix[] = "Cannot open leak data file ";
        write(2, ErrorMsgPrefix, strlen(ErrorMsgPrefix));
        write(2, pFileName, strlen(pFileName));
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
        write(2, pFileName, strlen(pFileName));
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
        write(2, pFileName, strlen(pFileName));
        write(2, " : ", 3);
        write(2, strerror(errno), strlen(strerror(errno)));
        write(2, "\n", 1);
        exit(0);
    }
    close(fd);

    for(p = pData; p < (pData + State.st_size); )
    {
#define FileNameSize 256
        SInfoBlock InfoBlock;
        char FileName[FileNameSize];
        int  len;
        int  FileNameLen;
        int  N = 0;

        memcpy(&InfoBlock, p, sizeof(InfoBlock)); p += sizeof(InfoBlock);
        InfoBlock.m_pCode = (char*)p;

        for(len = 0; len < InfoBlock.m_CodeStringLen; )
        {
            int RealFileNameLen;

            memcpy(&RealFileNameLen, p, sizeof(int));
            p += RealFileNameLen + 2 * sizeof(int);
            len += RealFileNameLen  + 2 * sizeof(int);
        }
        if(InfoBlock.m_Size < 0)
            if(LEAK_MODE == Mode)
                CheckFree(&InfoBlock);
            else
                CheckPoison(&InfoBlock);
        else
            AddAlloc(&InfoBlock);
    }

    if(LEAK_MODE == Mode)
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

                /* Store the matched data */
                memcpy(pGoodBlock + j, pBadBlock + i, sizeof(SInfoBlock));
                pGoodBlock[j].m_p = (void*)1;
                pBadBlock[i].m_p = NULL;
                return;
            }
        }
    }
}

static void CheckPoison(SInfoBlock* pInfoBlock)
{
    int i = 0;
    /* Match free and alloc */
    for(i = 0; i < BadBlockNum; i++)
    {
        if(pBadBlock[i].m_p == pInfoBlock->m_p)
        {
            /* Report the poison corruption */
            printf("The %s poison area is corrupted for the block of %d bytes\n",
                   pInfoBlock->m_Size == -1 ? "front" : "back", pBadBlock[i].m_Size
                  );
            printf("allocated at:\n");
            PrintSourceCodeInfo(pBadBlock + i);
            printf("\n\n and released at:\n");
            PrintSourceCodeInfo(pInfoBlock);
            pBadBlock[i].m_p = NULL;
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
    char  FileName[MAX_FILE_NAME_SIZE + 1];
    int   FileNameLen, RealFileNameLen;
    char* p;
    int   len, N;

    N = 0;
    p = pInfoBlock->m_pCode;
    for(len = 0; len < pInfoBlock->m_CodeStringLen; )
    {
        memcpy(&RealFileNameLen, p, sizeof(int)); p += sizeof(int); len += sizeof(int);
        if(RealFileNameLen >= MAX_FILE_NAME_SIZE)
            FileNameLen = MAX_FILE_NAME_SIZE;
        else
            FileNameLen = RealFileNameLen;

        memcpy(FileName, p + RealFileNameLen - FileNameLen, FileNameLen); p += RealFileNameLen; len += RealFileNameLen;
        FileName[FileNameLen] = '\0';

        memcpy(&RealFileNameLen, p, sizeof(int)); p += sizeof(int); /* This is the line number now */
        len += sizeof(int);

        printf("#%04d %s: %d\n", N++, FileName, RealFileNameLen);
    }
    printf("\n");
}

#endif
