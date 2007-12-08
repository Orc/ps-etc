/*
 * read the process table, build a linked list describing the
 * process heirarchy.
 */
#include "config.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#if USE_SYSCTL
# include <sys/sysctl.h>
#endif

#include "ptree.h"

static STRING(Proc) unsort = { 0 };

static int
compar(void *c1, void *c2)
{
    Proc *a = (Proc*)c1;
    Proc *b = (Proc*)c2;

    return a->pid  - b->pid;
}


static Proc*
append(pid_t pid, pid_t ppid, uid_t uid, gid_t gid,
       time_t ctime, char status, char process[])
{
    Proc *t = &EXPAND(unsort);
    
    bzero(t, sizeof *t);
    t->parent = (Proc*)-1;
    t->children = -1;
    t->pid = pid;
    t->ppid = ppid;
    t->uid = uid;
    t->gid = gid;
    t->ctime = ctime;
    t->status = status;
    strncpy(t->process, process, sizeof t->process);
    return t;
}


#if !USE_SYSCTL
static int
ingest(struct dirent *de, int flags)
{
    FILE *f;
    Proc *t;
    char *p;
    int ct, c, rc;
    pid_t pid, ppid;
    char status;
    char name[200];
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
	ct = fscanf(f, "%d (%200s %c %d",  &pid, name, &status, &ppid);
	fclose(f);

	if ( strlen(name) && (name[strlen(name)-1] == ')') )
	    name[strlen(name)-1] = 0;
#elif OS_FREEBSD
#define REQUIRED 3
	ct = fscanf(f, "%s %d %d", name, &pid, &ppid);
#else
# error "This OS is not supported (sorry!)"
#endif

	if ( ct == REQUIRED ) {
	    t = append(pid,ppid,st.st_uid,st.st_gid,st.st_ctime,status,name);
	    if ( !t ) return 0;

	    if ( (flags & PTREE_ARGS) && (f = fopen("cmdline", "r")) ) {
		while ( (c = getc(f)) != EOF ) {
		    if ( T(t->cmdline) )
			EXPAND(t->cmdline) = c;
		    else if ( c == 0 )
			CREATE(t->cmdline);
		}
		fclose(f);
	    }
	}
	chdir("..");
	return S(unsort);
    }
    return 0;
}
#endif


Proc *
pfind(pid_t pid)
{
    Proc key;

    key.pid = pid;

    return bsearch(&key, T(unsort), S(unsort), sizeof T(unsort)[0], compar);
}


static int
children(Proc *p, int countsibs)
{
    int count = 1;

    if ( !p ) return 0;
    if ( p->children != -1 ) return p->children;

    count += children(p->child, 1);

    if ( countsibs )
	while ( p = p->sib )
	    count += children(p->child, 1);

    return count;
}


static void
shuffle()
{
    int todo = S(unsort);
    int i;
    Proc *p, *my;

    while (todo > 0)
	for (i=0; i < S(unsort); i++) {
	    my = &T(unsort)[i];
	    if (my->parent == (Proc*)-1) {
		--todo;
		if ( (my->pid != my->ppid) && (p = pfind(my->ppid)) ) {
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
		else
		    my->parent = 0;
	    }
	}

    for (i=0; i < S(unsort); i++)
	T(unsort)[i].children = children(&T(unsort)[i], 0);
}


Proc *
ptree(int flags)
{
#if USE_SYSCTL
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
    struct kinfo_proc *job;
    size_t jsize;
    int njobs;
    Proc *tj;
    int i, rc = 0;

    if ( sysctl(mib, 4, NULL, &jsize, NULL, 0) != 0 )
	return 0;

    if ( !(job = malloc(jsize)) )
	return 0;

    if ( sysctl(mib, 4, job, &jsize, NULL, 0) != 0 ) {
	free(job);
	return 0;
    }
    
    njobs = jsize / sizeof job[0];

    S(unsort) = 0;
    for (i=0; i < njobs ; i++) {
	tj = append(job[i].kp_proc.p_pid,
		    job[i].kp_eproc.e_ppid,
		    job[i].kp_eproc.e_pcred.p_ruid,
		    job[i].kp_eproc.e_pcred.p_rgid,
		    (time_t)0,
		    'r',
		    job[i].kp_proc.p_comm);
	if ( !tj ) {
	    free(job);
	    return 0;
	}
    }
    free(job);
#else
    DIR *d;
    struct dirent *de;
    
    int home = open(".", O_RDONLY);

    if ( (home == -1) || (chdir("/proc") == -1) ) return 0;

    S(unsort) = 0;
    if ( d = opendir(".") ) {
	while (de = readdir(d))
	    if ( ingest(de, flags) == -1 ) {
		fchdir(home);
		return 0;
	    }
	closedir(d);
    }
    fchdir(home);
    close(home);
#endif
    qsort(T(unsort), S(unsort), sizeof T(unsort)[0], compar);

    shuffle();

    return pfind(1);
}
