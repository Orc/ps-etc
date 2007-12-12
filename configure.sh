#! /bin/sh

# local options:  ac_help is the help message that describes them
# and LOCAL_AC_OPTIONS is the script that interprets them.  LOCAL_AC_OPTIONS
# is a script that's processed with eval, so you need to be very careful to
# make certain that what you quote is what you want to quote.

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
# info via sysctl(), kvm_getprocs(), or the /proc filesystem.
# kvm_getprocs() needs to be done by root to get around protections
# on kmem, sysctl() needs to be done by root if you want to be
# able to get the argument lists of processes not owned by you,
# and /proc lets anyone grub through process commandlines.

# kvm_getprocs exists?
icanhaskvm() {
    if AC_QUIET AC_CHECK_HEADERS kvm.h sys/param.h sys/sysctl.h; then
	if LIBS=-lkvm AC_QUIET AC_CHECK_FUNCS kvm_getprocs; then
	    LOG " kvm_getprocs()"
	    AC_DEFINE USE_KVM
	    AC_SUB 'MKSUID' 'chmod +s'
	    _proc=kvm
	    AC_LIBS="$AC_LIBS -lkvm"
	    return 0
	fi
    fi
    return 1
}

# darwinish (also FreeBSD 6, other recent *BSDs?) sysctl()
icanhassysctl() {
    if AC_QUIET AC_CHECK_HEADERS sys/sysctl.h; then
	if AC_QUIET AC_CHECK_FUNCS sysctl; then
	    if AC_QUIET AC_CHECK_STRUCT kinfo_proc sys/types.h sys/sysctl.h;then
		LOG " sysctl()"
		AC_DEFINE USE_SYSCTL
		AC_SUB 'MKSUID' 'chmod +s'
		return 0
	    fi
	fi
    fi
    return 1
}

# traditional old,slow,but human readable /proc
# (only defined for freebsd and linux, sorry)
icanhasproc() {
    LOGN " /proc"
    AC_DEFINE USE_PROC
    AC_SUB 'MKSUID' ':'

    if [ "$OS_LINUX" ]; then
	AC_DEFINE STATFILE \"stat\"
	AC_DEFINE STATSCANFOK 4
	AC_DEFINE 'STATSCANF(f,p,pp,n,s)' \
		  'fscanf(f, "%d (%200s %c %d", p, n, s, pp)'
	LOG " (linux -> /proc/*/stat)"
	return 1
    elif [ "$OS_FREEBSD" ]; then
	AC_DEFINE STATFILE \"status\"
	AC_DEFINE STATSCANFOK 3
	AC_DEFINE 'STATSCANF(f,p,pp,n,s)' 'fscanf(f, "%s %d %d", n, p, pp)'
	LOG " (freebsd -> /proc/*/status)"
	return 1
    else
	LOG " (not defined)"
	AC_FAIL "Sorry, but /proc access is only defined on Linux and FreeBSD"
    fi
    return 0
}

LOGN "get process information from"

icanhassysctl || icanhaskvm || icanhasproc

AC_DEFINE CONFDIR '"'$AC_CONFDIR'"'

AC_OUTPUT Makefile
