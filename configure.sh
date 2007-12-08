#! /bin/sh

# local options:  ac_help is the help message that describes them
# and LOCAL_AC_OPTIONS is the script that interprets them.  LOCAL_AC_OPTIONS
# is a script that's processed with eval, so you need to be very careful to
# make certain that what you quote is what you want to quote.

ac_help='
--with-posix-at		be more comparable with the rest of the Unix world
--with-spooldir		where at jobs are spooled (/var/spool/cron/atjobs)'

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

AC_CHECK_HEADERS pwd.h grp.h ctype.h

[ "$OS_FREEBSD" -o "$OS_DRAGONFLY" ] || AC_CHECK_HEADERS malloc.h

# check to see if we're on a platform that supports getting proc
# info via sysctl()
if AC_CHECK_HEADERS sys/sysctl.h; then
    if AC_CHECK_FUNCS sysctl; then
	if AC_CHECK_STRUCT kinfo_proc sys/types.h sys/sysctl.h; then
	    AC_DEFINE USE_SYSCTL
	fi
    fi
fi

AC_DEFINE CONFDIR '"'$AC_CONFDIR'"'

AC_OUTPUT Makefile
