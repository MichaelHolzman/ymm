#include <time.h>   /* For leak check based on time */
#include <limits.h> /* For LONG_MAX */

int yamm_max_thread_num  = 100;
int yamm_make_statistics = 0;
int yamm_debug_level     = 0;
int yamm_double_free     = 0;
int yamm_sizes[] = { 21, 33, 65, 73, 89, 98, 165, 169, 209, 257, 304, 577, 2264, 4513, 20000, 30000, 100000, 200000, 300000, 400000, 500000, -1};
int yamm_is_dumper_on = 0;
int yamm_dump_interval = 900;
int yamm_poison_check_enabled = 1;

#ifdef LINUX
unsigned long long yamm_leak_check_start_time = LONG_MAX;
unsigned long long yamm_leak_check_stop_time = 10;
unsigned long long yamm_poison_check_start_time = LONG_MAX;
unsigned long long yamm_poison_check_stop_time = 10;
#else
hrtime_t yamm_leak_check_start_time = LONG_MAX;
hrtime_t yamm_leak_check_stop_time = 2;
hrtime_t yamm_poison_check_start_time = LONG_MAX;
hrtime_t yamm_poison_check_stop_time = 2;
#endif
