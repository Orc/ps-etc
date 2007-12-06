#ifndef _PTREE_D
#define _PTREE_D

#include <unistd.h>
#include <time.h>

typedef struct proc {
    struct proc *parent;
    struct proc *child;
    struct proc *sib;
    pid_t pid, ppid;
    uid_t uid, gid;
    time_t ctime;
    char process[80];
    char status;
} Proc;

Proc *ptree();
Proc *pfind(pid_t);

#endif
