# Introduction #

YAMM is a very simple and fast implementation of malloc()/free() suit for Unix-like systems. It’s fast, almost non-blocking and configurable. It has been tested on HP-UX, Solaris, AIX, and Linux. It can be built and used in 32- and 64-bit mode and support any application using standard malloc()/free() calls.
YAMM should easily ported to any platform supporting shared library preload and mmap() call


# Features #

First of all, YAMM is a very fast memory allocator. The more threads an application has the bigger the benefit. To achieve more one can [tune](tune.md) it. And, the last application of YAMM is memory [fighting](leak.md). The leak diagnostic is quite primitive as of now and is supported on HP-UX only. It will be improved if requested.

# Installation #

The YAMM installation is straightforward:
  1. Create an installation directory
  1. Put the source files and scripts on the created directory
  1. Run `yamm_build.ksh` to get 64-bit version or `yamm_build.ksh  32` to get 32-bit version of libraries.
  1. Verify that the pair of `libyamm.sl` and `libyamm_tune.sl` or the pair of `libyamm64.sl` and `libyamm_tune64.sl` libraries is created

# Using YAMM #

There are two ways of using it. It can be added to the link line of application like any other library. It can also be invocated using environment variable `LD_PRELOAD` (it might have another name on some systems/modes). In that case, the application remains intact but uses another way of dealing with memory allocation.

Here are the examples:
Let us say we have source files for application `OurApp`: `OurApp.c OurAppData.cpp`. We want YAMM to be linked so nobody can use ordinary malloc() by default. To do that one need to build `OurApp` like (for HP-UX 32-bit mode):

> `/opt/aCC/bin/aCC –o OurApp OurApp.c OurAppData.cpp –lyamm_tune –lyamm`

Another project wants to run the existent 64-bit application `TheirApp` with YAMM. They do not have sources and cannot rebuild the application. So they have to use `LD_PRELOAD`. To do that they need to define the environment variable

> `export LD_PRELOAD=”/yam_path/libyamm_tune64.so:/yam_path/libyamm64.so”`

and then run the application as usual.


##### Note #####
It might be required to add `/usr/lib/librt.so` to the `LD_PRELOAD` environment variable on Linux