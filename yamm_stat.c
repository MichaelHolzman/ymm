#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <yamm.h>

static int Counters[MAX_TRACED_SIZE];

static void Init();
static void ProcessFile(char* pFileName);
static void PrintResults();

int main(int argc, char* argv[])
{
    int i;

    if(argc < 2)
    {
        printf("Usage: %s {<file name>}...\n", argv[0]);
        exit(1);
    }

    Init();
    for(i = 1; i < argc; i++)
    {
        ProcessFile(argv[i]);
    }
    PrintResults();
    return 1;
}

static void Init()
{
    int i;

    for(i = 0; i < MAX_TRACED_SIZE; i++)
        Counters[i] = 0;
}

static void ProcessFile(char* pFileName)
{
    int fd, i;

    fd = open(pFileName, O_RDONLY);
    if(fd < 0)
    {
        printf("Can't open %s for reading\n", pFileName);
        return;
    }

    for(i = 0; i < MAX_TRACED_SIZE; i++)
    {
        static int Buf;

        if(sizeof(int) != read(fd, &Buf, sizeof(int)))
        {
            printf("File %s is shorter than expected\n", pFileName);
            return;
        }
        else
        {
            if(Buf)Counters[i] += Buf;
        }
    }
    close(fd);
}

static void PrintResults()
{
    int i;

    printf("YAMM statistics\nsize  counter\n");
    for(i = 0; i < MAX_TRACED_SIZE; i++)
    {
        if(Counters[i] > 0)
            printf("%-7d\t%-7d\n", i, Counters[i]);
    }
}
