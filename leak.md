# General Desription #

YAMM provides a very simple memory leak detection ability. After each memory block allocating, the block size and pointer are stored in the file named `yamm_leak.dat`. The file is located on the current work directory. This file is processed later by the `yamm_leak_report` application. The report application does not require command line parameters. It looks for the `yamm_leak.dat` file in the current working directory and produces a report if the file is found.

Leak report contains messages of two types. The first type named _**'Potential memory leak'**_. An error of this type means that several allocations were done at this point of code but not all of them were freed.

The second error type is _**'Memory leak'**_. An error of this type means a pure memory leak: some space were allocated at this point of code and never released.

# Configuration for Leak Detection #

The memory leak monitoring is controlled by two variables set in `yamm_tune.c` (see [tuning](tune.md)):

```
extern hrtime_t yamm_leak_check_start_time = 2;
extern hrtime_t yamm_leak_check_stop_time = 150;
```

The first variable says when to start monitoring. It is the time (in seconds) from the application start. The latter defines when to stop monitoring. In the example above the monitoring will start two seconds after the invocation and will stop 148 seconds afterwards (150 seconds from the beginning).

A negative value of `yamm_leak_check_start_time` disables the monitoring.

# Example #
Suppose we need to configure the YAMM for leak detection. We start with creating the `yamm_tune.c` with the following contents
```
extern int yamm_max_thread_num  = 1000;
extern int yamm_make_statistics = 0;
extern int yamm_debug_level     = 0;
extern int yamm_double_free     = 0;
extern int yamm_sizes[] = { 64, 256, 512, 1024, 2048, 4096, 8192, 16000, 32000, 64000, 128000, 256000, 512000, -1};
extern hrtime_t yamm_leak_check_start_time = 2;
extern hrtime_t yamm_leak_check_stop_time = 150;
```

expecting up to 1000 threads (`yamm_max_thread_num`), without statistic output (`yamm_make_statistics = 1`), and using blocks of 64, 256, etc. (`yamm_sizes`) bytes. We want to begin leak checking **2** seconds after the application start and end **150** second after the start. Rest of the variables should be left intact. They are used for the development of YAMM itself.

We build the libraries when the file is ready and run the target application with YAMM for more than 150 seconds. The file `yamm_leak.dat` should be created in the current working directory when the required 150 seconds are over.

If the file is produced and not empty, we run `yamm_leak_report` application and see the report on the terminal

### Linux Notes ###
  1. `addr2line` must be installed to get the source file and line number information
  1. `yamm_leak_check_start_time` should be set to `LONG_MAX` to disable leak check
  1. If `yamm_leak_report` fails to provide the source file name and line number, it can be obtained manually by running `addr2line -ifC -e <binary file> <address>`
  1. It might be required to add `/usr/lib/librt.so` to the `LD_PRELOAD` environment variable