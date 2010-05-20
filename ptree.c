/*
 * read the process table, build a linked list describing the
 * process heirarchy.
 *
 * There's a whole lot of sausage making going on in this
 * module.
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
#elif USE_KVM
# include <kvm.h>
# include <sys/param.h>
# include <sys/user.h>
# include <sys/sysctl.h>
#endif

#include <libgen.h>

#include "ptree.h"

static STRING(Proc) unsort = { 0 };

static int
compar(void *c1, void *c2)
{
    Proc *a = (Proc*)c1;
    Proc *b = (Proc*)c2;

    return a->pid  - b->pid;
}


/* allocate and initialize a slot in the unsort array
 */
static Proc*
another(char process[])
{
    Proc *t = &EXPAND(unsort);
    
    if ( t ) {
	bzero(t, sizeof *t);
	t->parent = (Proc*)-1;
	t->children = -1;
	strncpy(t->process, process, sizeof t->process);
    }
    return t;
}


#if USE_PROC
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

    if (chdir(de->d_name) == 0) {
	if ( (stat(".", &st) != 0) || !(f = fopen(STATFILE, "r")) ) {
	    chdir("..");
	    return 0;
	}
	ct = STATSCANF(f, &pid, &ppid, name, &status);
	fclose(f);

#ifdef OS_LINUX
	if ( strlen(name) && (name[strlen(name)-1] == ')') )
	    name[strlen(name)-1] = 0;
#endif

	if ( ct == STATSCANFOK ) {

	    if ( !(t = another(name)) ) return 0;

	    t->pid = pid;
	    t->ppid = ppid;
	    t->uid = st.st_uid;
	    t->gid = st.st_gid;
	    t->ctime = st.st_ctime;
	    t->status = status;

	    if ( (flags & PTREE_ARGS) && (f = fopen("cmdline", "r")) ) {
		CREATE(t->cmdline);
		while ( (c = getc(f)) != EOF ) {
		    if ( c || S(t->cmdline) )
			EXPAND(t->cmdline) = c;
		}
		t->renamed = strcmp(basename(T(t->cmdline)), name);
		fclose(f);
	    }
	}
	chdir("..");
	return S(unsort);
    }
    return 0;
}
#endif


static int
getprocesses(int flags)
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

    for (i=0; i < njobs ; i++) {
	if ( tj = another(job[i].kp_proc.p_comm) ) {
	    tj->pid = job[i].kp_proc.p_pid;
	    tj->ppid = job[i].kp_eproc.e_ppid;
	    tj->uid = job[i].kp_eproc.e_pcred.p_ruid;
	    tj->gid = job[i].kp_eproc.e_pcred.p_rgid;

	    if ( flags & PTREE_ARGS ) {
		struct {
		    int count;
		    char rest[4096-sizeof(int)];
		} args;
		size_t argsize;
		char *p;
		
		CREATE(tj->cmdline);

		mib[1] = KERN_PROCARGS2;
		mib[2] = tj->pid;

		argsize = sizeof args;
		if ( sysctl(mib,3,&args,&argsize,NULL,0) == 0 ) {

		    p = args.rest;

		    if ( args.count > 0 ) {
			while ( !*p ) ++p;
			while ( *p ) ++p;
			while ( !*p) ++p;
		    }
		    
		    while (args.count-- > 0) {
			do {
			    if ( p >= args.rest + sizeof args.rest )
				goto overflow;
			    if ( *p || S(tj->cmdline) )
				EXPAND(tj->cmdline) = *p;
			} while (*p++);
		    }
		    tj->renamed = strncmp(basename(T(tj->cmdline)), tj->process, strlen(tj->process));
	    overflow: ;
		}
	    }
	}
	else {
	    free(job);
	    return 0;
	}
    }
    free(job);
#elif USE_KVM
    struct kinfo_proc *job;
    kvm_t *k;
    Proc *tj;
    int j, i, njobs;
    char **av, *p, *q;

    if ( !(k = kvm_openfiles(NULL, NULL, NULL, O_RDONLY, NULL)) )
	return 0;

    if ( !(job = kvm_getprocs(k, KERN_PROC_ALL, 0, &njobs)) ) {
	kvm_close(k);
	return 0;
    }

    for (i=0; i < njobs; i++)
#if FREEBSD_7_KVM
# define kpid  ki_pid
# define kppid ki_ppid
# define kuid  ki_ruid
# define kgid  ki_rgid
# define kname ki_comm
#else
# define kpid  kp_proc.p_pid
# define kppid kp_eproc.e_ppid
# define kuid  kp_eproc.e_pcred.p_ruid
# define kgid  kp_eproc.e_pcred.p_rgid
# define kname kp_proc.p_comm
#endif
	if ( tj = another(job[i].kname) ) {
	    tj->pid = job[i].kpid;
	    tj->ppid =job[i].kppid;
	    tj->uid = job[i].kuid;
	    tj->gid = job[i].kgid;

	    if ( flags & PTREE_ARGS ) {

		if ( (av = kvm_getargv(k,&job[i],0)) && *av ) {
		    CREATE(tj->cmdline);
		    for ( j=0; av[j]; ++j ) {
			if ( S(tj->cmdline) ) EXPAND(tj->cmdline) = 0;
			p = av[j];
			while ( *p )
			    EXPAND(tj->cmdline) = *p++;
		    }
		    tj->renamed = strcmp(basename(T(tj->cmdline)), tj->process);
		}
	    }
	}
	else {
	    kvm_close(k);
	    return 0;
	}
    kvm_close(k);

#else /*USE_PROC*/
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
    return 1;
}


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
			Proc *nc;
			for (nc = p->child; nc->sib; nc = nc->sib)
			    ;
			nc->sib = my;
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
    S(unsort) = 0;

    if ( getprocesses(flags) == 0 )
	return 0;

    qsort(T(unsort), S(unsort), sizeof T(unsort)[0], compar);

    shuffle();

    return pfind(1);
}
