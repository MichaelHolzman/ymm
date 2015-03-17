# Tuning YAMM #

YAMM can be tuned. It means that one can configure couple of the run-time parameters to achieve better performance and less memory footprint.


# A few words about the architecture #

YAMM allocates memory blocks of predefined sizes only. Each thread has one list of free blocks of each size. The lists are independent and are initialized at the first request to the list. If for example YAMM configured to allocate 37, 148, 371-byte blocks a request for 128-byte block will result in allocation of a 148-byte block. The 20 (148-128) bytes will not be used and that is the waste of memory.

Another possible waste of memory is the thread control structures holding some information about the running threads.

The optimal configuration would know the maximum number of threads and size of each request to malloc(). It’s not possible in advance as there might be threads created by system or 3rd party libraries and there might be calls to malloc() from those threads and libraries.

# Tuning Details #

The tuning is a two-step process. First, YAMM should be run in statistic gathering mode. It will produce a file per each thread with information about number and size of allocation requests for that thread. That information should be processes and the decision about the number of threads and sizes of blocks should be made. Then, there should be created a shared library `libyamm_tune.so` (or `libyamm_tune64.so`) containing appropriated definitions. The newly created library should then be used at run time. Note that file extension differ on some platforms. It might  be .sl, .so,  or .a

**Note.** The YAMM allows getting the statistics data of a single thread. Since the raw statistics of each thread is stored in a separate file named `yamm_stat_<PID><PTHREAD_ID>.dat`, one can run

`yamm_stat  yamm_stat_00186_0000003.dat`

to see the allocation statistics of the thread 3 of the process with PID = 186

# Example #

Let’s say we have an application that needs to be tuned.

  1. We need to get the statistics. To do that we create the yamm\_tune.c with the following contents
```
extern int yamm_max_thread_num  = 1000;
extern int yamm_make_statistics = 1;
extern int yamm_debug_level     = 0;
extern int yamm_double_free     = 0;
extern int yamm_sizes[] = { 64, 256, 512, 1024, 2048, 4096, 8192, 16000, 32000, 64000, 128000, 256000, 512000, -1};
```
    * up to 1000 threads expected (yamm\_max\_thread\_num)
    * statistic output is required (yamm\_make\_statistics = 1)
    * blocks of 64, 256, etc. (yamm\_sizes) bytes are used
    * rest of the variables should be left intact. They are used for the development of YAMM itself.

> 2. We complie yamm\_tune.c and create libtamm\_tune.so

> 3. We run the application and get files named like `yamm_stat_<pid>_<thread id>.dat`:
```
-rw-r--r--   1 skywalk    pm 2000000 Jun 16 08:35 yamm_stat_0018695_0000001.dat
```

> 4. The maximum number of thread in file names gives the appropriate value for yamm\_max\_thread\_num (I would take some extra threads)

> 5. We run yamm\_stat
```
yam_stat yamm_stat_0018695_*.dat
```
> and see the output in the form
```
YAMM statistics
(size cout)
32          130
40          2  
88          1  
120         1  
190         3  
191         3  
192         3  
193         3  
194         3  
195         3  
196         3  
197         3  
198         3  
199         3  
200         3  
8622        1  
11264       1  
16814       1  
33198       1  
65966       1  
```
> 6. We decide that we need mostly bloks of 32 bytes, some of 120 to 200 bytes, and couple of big ones. So the `yamm_sizes` will look like
```
extern int yamm_sizes[] = { 34, 202, -1};
```
> meaning that each thread will manipulate wit blocks of 34 and 202 byte size (I always prefer to have a couple of bytes spare to avoid stupid buffer overruns). Blocks bigger that 202 bytes will be allocated directly from the OS with no intervention.

> 7. New `libyamm_tune.so` library is then created with `yamm_make_statistics = 0` and used in run time.