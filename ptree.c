#include "config.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "ptree.h"

static Proc *init = 0;

static Proc *unsort = 0;
static int szu = 0;
static int nru = 0;

static int
compar(void *c1, void *c2)
{
    Proc *a = (Proc*)c1;
    Proc *b = (Proc*)c2;

    return a->pid  - b->pid;
}


static int
ingest(struct dirent *de, int flags)
{
    FILE *f;
    char *p;
    int ct;
    int c;
    pid_t pid, ppid;
    char status;
    char process[200];
    struct stat st;

    for (p = de->d_name; *p; ++p)
	if (!isdigit(*p))
	    return 0;

#ifdef OS_LINUX
#define STATFILE "stat"
#elif OS_FREEBSD
#define STATFILE "status"
#else
#error "No support for this OS"
#endif

    if (chdir(de->d_name) == 0) {
	if ( (stat(".", &st) != 0) || !(f = fopen(STATFILE, "r")) ) {
	    chdir("..");
	    return 0;
	}

#ifdef OS_LINUX
#define REQUIRED 4
	ct = fscanf(f, "%d (%200s %c %d",  &pid, process, &status, &ppid);
	fclose(f);

	if ( strlen(process) && (process[strlen(process)-1] == ')') )
	    process[strlen(process)-1] = 0;
#elif OS_FREEBSD
#define REQUIRED 3
	ct = fscanf(f, "%s %d %d", process, &pid, &ppid);
#else
# error "This OS is not supported (sorry!)"
#endif

	if ( ct == REQUIRED ) {
	    if (nru >= szu) {
		szu = 10 + (szu*10);
		unsort = unsort ? realloc(unsort, szu * sizeof(*unsort) ) : malloc( szu * sizeof(*unsort) );
		if (unsort == 0) {
		    szu = nru = 0;
		    return -1;
		}
	    }
	    bzero(&unsort[nru], sizeof unsort[nru]);
	    unsort[nru].pid = pid;
	    unsort[nru].ppid = ppid;
	    unsort[nru].uid = st.st_uid;
	    unsort[nru].gid = st.st_gid;
	    unsort[nru].ctime = st.st_ctime;
	    unsort[nru].status = status;
	    strncpy(unsort[nru].process, process, sizeof unsort[nru].process);

	    if ( (flags & PTREE_ARGS) && (f = fopen("cmdline", "r")) ) {
		while ( (c = getc(f)) != EOF ) {
		    if ( T(unsort[nru].cmdline) )
			EXPAND(unsort[nru].cmdline) = c;
		    else if ( c == 0 )
			CREATE(unsort[nru].cmdline);
		}
		fclose(f);
	    }
	    nru++;
	}
	chdir("..");
	return nru;
    }
    return 0;
}


Proc *
pfind(pid_t pid)
{
    Proc key;

    key.pid = pid;

    return bsearch(&key, unsort, nru, sizeof unsort[0], compar);
}


static void
shuffle()
{
    int todo = nru;
    int i;
    Proc *p, *my;

    while (todo > 0)
	for (i=0; i < nru; i++) {
	    my = &unsort[i];
	    if (my->parent == 0) {
		--todo;
		if (my->pid == 1)  {
		    my->parent = init = my;
		}
		else if (p = pfind(my->ppid)) {
		    my->parent = p;

		    if (p->child) {
			Proc *shuffle;
			for (shuffle = p->child; shuffle->sib; shuffle = shuffle->sib)
			    ;
			shuffle->sib = my;
		    }
		    else
			p->child = my;
		}
	    }
	}
}


Proc *
ptree(int flags)
{
    DIR *d;
    struct dirent *de;
    int home = open(".", O_RDONLY);

    if ( (home == -1) || (chdir("/proc") == -1) ) return 0;

    nru = 0;
    init = 0;
    if ( d = opendir(".") ) {
	while (de = readdir(d))
	    if ( ingest(de, flags) == -1 ) {
		fchdir(home);
		return 0;
	    }
	closedir(d);
	qsort(unsort, nru, sizeof unsort[0], compar);
    }
    fchdir(home);
    close(home);

    shuffle();

    return init;
}
