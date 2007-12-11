#include "config.h"

#include <stdio.h>
#include <stdarg.h>
#include <sys/ioctl.h>

#ifdef HAVE_TERMIOS_H
# include <termios.h>
#elif HAVE_TERMIO_H
# include <termio.h>
#elif HAVE_SGTTY_H
# include <sgtty.h>
#endif

#include "cstring.h"

static STRING(char) line;


int
printcard(char *fmt, ...)
{
    int size;
    va_list ptr;

    va_start(ptr,fmt);

    if ( T(line) ) {
	size = vsnprintf(T(line)+S(line),line.alloc-S(line), fmt, ptr);
	S(line) += size;
    }
    else
	size = vprintf(fmt, ptr);

    va_end(ptr);

    return size;
}


int
putcard(char c)
{
    if ( !T(line) )
	putchar(c);
    else if ( S(line) < line.alloc )
	EXPAND(line) = c;

    return 1;
}


void
ejectcard()
{
    if (T(line)) {
	int len = S(line);

	if (len >= line.alloc) len = line.alloc-1;
	fwrite(T(line), len, 1, stdout);
	S(line) = 0;
    }
    putchar('\n');
}


void
cardwidth()
{
    int width = 80;

#if defined(TIOCGSIZE)
    struct ttysize tty;
    if (ioctl(0, TIOCGSIZE, &tty) == 0) {
	if (tty.ts_cols)  width=tty.ts_cols;
    }
#elif defined(TIOCGWINSZ)
    struct winsize tty;
    if (ioctl(0, TIOCGWINSZ, &tty) == 0) {
	if (tty.ws_col) width=tty.ws_col;
    }
#endif
    RESERVE(line, width+1);
    S(line) = 0;
}
