#! /bin/sh

# local options:  ac_help is the help message that describes them
# and LOCAL_AC_OPTIONS is the script that interprets them.  LOCAL_AC_OPTIONS
# is a script that's processed with eval, so you need to be very careful to
# make certain that what you quote is what you want to quote.

ac_help='
--use-kvm		use kvm_getprocs() if at all possible'

# load in the configuration file
#
TARGET=psetc
. ./configure.inc

AC_INIT $TARGET

AC_PROG_CC

case "$AC_CC $AC_CFLAGS" in
*-Wall*)    AC_DEFINE 'while(x)' 'while( (x) != 0 )'
	    AC_DEFINE 'if(x)' 'if( (x) != 0 )' ;;
esac

AC_C_VOLATILE
AC_C_CONST
AC_SCALAR_TYPES
AC_CHECK_ALLOCA || AC_FAIL "$TARGET requires alloca()"

AC_CHECK_FUNCS scandir || AC_FAIL "$TARGET requires scandir()"

# for basename
if AC_CHECK_FUNCS basename; then
    AC_CHECK_HEADERS libgen.h
fi

AC_CHECK_HEADERS pwd.h
AC_CHECK_HEADERS grp.h
AC_CHECK_HEADERS ctype.h
AC_CHECK_HEADERS termios.h
AC_CHECK_HEADERS termio.h
AC_CHECK_HEADERS sgtty.h

[ "$OS_FREEBSD" -o "$OS_DRAGONFLY" ] || AC_CHECK_HEADERS malloc.h

# check to see if we're on a platform that supports getting proc
# info via sysctl().    kvm_getprocs() access needs to be explicitly
# requested because you have to be root to read the kernel image,
# while /proc and sysctl() can be read by anyone.

icanhaskvm() {
    if AC_QUIET AC_CHECK_HEADERS kvm.h sys/param.h sys/sysctl.h; then
	if LIBS=-lkvm AC_QUIET AC_CHECK_FUNCS kvm_getprocs; then
	    LOG " kvm_getprocs()"
	    AC_DEFINE USE_KVM
	    AC_SUB 'MKSUID' 'chmod +s'
	    _proc=kvm
	    AC_LIBS="$AC_LIBS -lkvm"
	    return
	fi
    fi
}

LOGN "get process information from"
unset _proc

test "$USE_KVM" && icanhaskvm

if [ -z "$_proc" ]; then
    if AC_QUIET AC_CHECK_HEADERS sys/sysctl.h; then
	if AC_QUIET AC_CHECK_FUNCS sysctl; then
	    if AC_QUIET AC_CHECK_STRUCT kinfo_proc sys/types.h sys/sysctl.h;then
		AC_DEFINE USE_SYSCTL
		_proc=sysctl
	    fi
	fi
    fi > /dev/null

    test "$_proc" = "sysctl" && LOG " sysctl()"
fi

test "$_proc" || icanhaskvm

if test -z "$_proc"; then
    LOGN " /proc"
    AC_DEFINE USE_PROC

    if [ "$OS_LINUX" ]; then
	AC_DEFINE STATFILE \"stat\"
	AC_DEFINE STATSCANFOK 4
	AC_DEFINE 'STATSCANF(f,p,pp,n,s)' \
		  'fscanf(f, "%d (%200s %c %d", p, n, s, pp)'
	LOG " (linux -> /proc/*/stat)"
    elif [ "$OS_FREEBSD" ]; then
	AC_DEFINE STATFILE \"status\"
	AC_DEFINE STATSCANFOK 3
	AC_DEFINE 'STATSCANF(f,p,pp,n,s)' 'fscanf(f, "%s %d %d", n, p, pp)'
	LOG " (freebsd -> /proc/*/status)"
    else
	LOG " (not defined)"
	AC_FAIL "Sorry, but /proc access is only defined on Linux and FreeBSD"
    fi
fi

if [ "_proc" != "kvm" ]; then
    AC_SUB 'MKSUID' ':'
fi

AC_DEFINE CONFDIR '"'$AC_CONFDIR'"'

AC_OUTPUT Makefile
