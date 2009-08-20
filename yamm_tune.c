#include <time.h>  /* For leak check based on time */

extern int yamm_max_thread_num  = 100;
extern int yamm_make_statistics = 0;
extern int yamm_debug_level     = 0;
extern int yamm_double_free     = 0;
extern int yamm_sizes[] = { 21, 33, 65, 73, 89, 98, 165, 169, 209, 257, 304, 577, 2264, 4513, 20000, 30000, 100000, 200000, 300000, 400000, 500000, -1};
extern int yamm_is_dumper_on = 0;
extern int yamm_dump_interval = 900;

#ifdef LINUX
long long yamm_leak_check_start_time = 1;
long long yamm_leak_check_stop_time = 10;
#else
extern hrtime_t yamm_leak_check_start_time = 1;
extern hrtime_t yamm_leak_check_stop_time = 2;
#endif
