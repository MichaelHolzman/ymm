#include <libgen.h>

#include "yamm_leak_hp.c"
#include "yamm_leak_linux.c"

int main(int argc, char* argv[])
{
    if(argc < 2)
    {
        printf("Usage: %s <data file name>\n", basename(argv[0]));
        exit(EXIT_SUCCESS);
    }

    if(strcmp("yamm_leak_report", basename(argv[0])) == 0)
        ReadAndProcessData(LEAK_MODE, argv[1]);
    else
        ReadAndProcessData(POISON_MODE, argv[1]);
    return 1;
}
