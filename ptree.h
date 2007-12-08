#ifndef _PTREE_D
#define _PTREE_D

#include <unistd.h>
#include <time.h>
#include "cstring.h"

typedef struct proc {
    struct proc *parent;
    struct proc *child;
    struct proc *sib;
    int children;
    int sorted;
    pid_t pid, ppid;
    uid_t uid, gid;
    time_t ctime;
    char process[80];
    Cstring cmdline;
    char status;
} Proc;

Proc *ptree(int);
Proc *pfind(pid_t);

#define PTREE_ARGS	0x01	/* populate Proc->cmdline */

#endif
