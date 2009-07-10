#!/usr/bin/env ksh

if [[ `uname` = SunOS ]]
then
    CC="cc -g"
    LIB_SUFF=so

    if [[ "$1" = 32 ]]
    then
        CCFLAGG="-mt -I. -DSOLARIS"
        LDFLAGS=-G
        BIT_SUFF=
    else
        CCFLAGG="-mt -xO2 -KPIC -xildoff -mt -xs -xarch=v9 -DNO_LIANT -DUNIX -DSOLARIS -D__sun__ -D_DEBUG -D__unix -DTHREAD_MODE  -DMT_SAFE -DTHREAD_MODE -DSUN_CC_HAS_PVFC_BUG -I."
        LDFLAGS="-L/usr/lib/sparcv9 -L/opt/SUNWspro//WS6U2/lib/v9 -L/opt/SUNWspro/lib/v9 -L/opt/SUNWspro/lib -L/usr/lib -lm -G"
        BIT_SUFF=64
    fi
fi

if [[ `uname` = Linux  ]]
then
    CC="gcc -g"
    LIB_SUFF=so
    CCFLAGG="-x c -Wall -O2 -DLINUX -I. -pthread "
    LDFLAGS="-shared "
    BIT_SUFF=
fi

if [[ `uname` = AIX ]]
then
    CC="/usr/vac/bin/xlc_r  -g"
    LIB_SUFF=a
    LDFLAGS="-G -bdynamic -brtl -bh:5 -qmkshrobj -L/usr/lib -L/usr/vac/lib -L/usr/lib/threads -L/usr/local64/lib -lpthreads"

    if [[ "$1" = 32 ]]
    then
	CCFLAGG="-DAIX -D__unix -D__aix__ -D__aix -D_AIX -Dunix -D_AIO_AIX_SOURCE -DNO_LIANT -DUNIX -U__MATH__ -qidirfirst -qstaticinline -qlanglvl=extended -qchars=signed -qnotempinc -qwarn64 -qthreaded -I/usr/vacpp//include -I/usr/local/include -I. -qsuppress=1506-742:1506-743"
	BIT_SUFF=
    else
	CCFLAGG="-q64 -DAIX -D__unix -D__aix__ -D__aix -D_AIX -Dunix -D_AIO_AIX_SOURCE -DNO_LIANT -DUNIX -U__MATH__ -qidirfirst -qstaticinline -qlanglvl=extended -qchars=signed -qnotempinc -qwarn64 -qthreaded -I/usr/vacpp//include -I/usr/local/include -I. -qsuppress=1506-742:1506-743"
	BIT_SUFF=64
    fi
fi

if [[ `uname` = HP-UX ]]
then
    CC="/usr/bin/cc -g -mt "
    LIB_SUFF=sl
    LDFLAGS="-b -Wl,+n -Wl,+s -Wl,+k "

    if [[ "$1" = 32 ]]
    then
	CCFLAGG="+Z -DHPUX -DCPF_HAS_INLINES -DCPF_DEBUG -DNO_LIANT -DUNIX -D_NET_IF6_INCLUDED -D_REENTRANT -D_RWSTD_MULTI_THREAD -D_POSIX_C_SOURCE=199506L -DHPUX_VERS=1111 +W890 +W930 +W302 +W2186 +O2 -I/usr -I/usr/include -I/usr/local64/include -I/usr/include/CC -I/usr/local/include -I. -lunwind"
	BIT_SUFF=
    else
	CCFLAGG="+DD64 +Z -DHPUX -DCPF_HAS_INLINES -DCPF_DEBUG -DNO_LIANT -DUNIX -D_NET_IF6_INCLUDED -D_REENTRANT -D_RWSTD_MULTI_THREAD -D_POSIX_C_SOURCE=199506L -DHPUX_VERS=1111 +W890 +W930 +W302 +W2186 +O2 -I/usr -I/usr/include -I/usr/local64/include -I/usr/include/CC -I/usr/local/include -I. -lunwind"
	BIT_SUFF=64
    fi
fi




echo "----------Build libyamm_tune  -----------"
$CC $CCFLAGG $LDFLAGS -o libyamm_tune${BIT_SUFF}.${LIB_SUFF} yamm_tune.c
echo "----------Build libyamm  -----------"
$CC $CCFLAGG $LDFLAGS -o libyamm${BIT_SUFF}.${LIB_SUFF} yamm.c
echo "----------Build yamm_stat-----------"
$CC $CCFLAGG -o yamm_stat${BIT_SUFF} yamm_stat.c
echo "----------Build yamm_report ---------------"
$CC $CCFLAGG -o yamm_leak_report yamm_leak_report.c
echo "----------Build yamm_tune ---------------"
$CC $CCFLAGG -o yamm${BIT_SUFF} yamm.c -D___MAIN_____ -L. -lyamm_tune${BIT_SUFF} -lpthread
