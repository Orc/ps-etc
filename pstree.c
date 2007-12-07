#include <stdio.h>
#include <pwd.h>
#include <sys/types.h>

#include "ptree.h"


int showargs = 0;
int compress = 1;
int clipping = 1;
int sortme   = 1;
int showpid  = 0;
int showuser = 0;

int
cmp(Proc *a, Proc *b)
{
    int rc = strcasecmp(a->process, b->process);

    if ( rc == 0 )
	rc = strcmp(a->process, b->process);

    if ( rc == 0 )
	return a->pid - b->pid;

    return rc;
}


#if 1
/* merge sort */
Proc *
sibsort(Proc *p)
{
    Proc *d, *t, *left = 0, *right = 0;
    int even = 0;

    if ( !(p && p->sib) ) return p;

    /* split into two lists */
    while (p) {
	t = p;
	p = p->sib;

	if (even) {
	    t->sib = left;
	    left = t;
	}
	else {
	    t->sib = right;
	    right = t;
	}
	even = !even;
    }

    /* sort them */
    if ( left ) left = sibsort(left);
    if ( right ) right = sibsort(right);

    /* merge them together */
    for ( p = t = 0; left && right;  ) {
	if (cmp(left,right) < 0) {
	    d = left;
	    left = left->sib;
	}
	else {
	    d = right;
	    right = right->sib;
	}
	if ( p )
	    t->sib = d;
	else
	    p = d;
	t = d;
    }
    t->sib = left ? left : right;

    return p;
}
#else
/* muddled sort */
Proc *
sibsort(Proc *p)
{
    Proc *a, *b, *t, *r;
    int rc;
    int i;

    if ( p == 0 ) return p;

    /* break off an initial sorted segment */
    for (i=0, a = r = p; r->sib && cmp(r,r->sib) > 0; r = r->sib)
	i++;

    if ( r->sib == 0 )		/* all sorted */
	return p;

    b = r->sib;
    r->sib = 0;			/* truncate first sorted list */
    b = sibsort(b);		/* sort remainder of list */

    /* merge the two sorted lists */

    p = t = b;
    b = b->sib;

    while ( a && b ) {
	if ( cmp(a,b) > 0 ) {
	    r = a;
	    a = a->sib;
	}
	else {
	    r = b;
	    b = b->sib;
	}
	t->sib = r;
	t = r;
    }
    t->sib = a ? a : b;

    return p;
}
#endif


static int _paren = 0;


int
po()
{
    if (_paren)
	return printf(",");
    _paren = 1;
    return printf("(");
}


int
pc()
{
    if (_paren) {
	_paren = 0;
	return printf(")");
    }
    return 0;
}


struct tabstack {
    int column;
    int active;
} stack[100];

int tsp = 0;


void
push(int column, int c)
{
    stack[tsp].column = column;
    stack[tsp].active = c;
    ++tsp;
}


void
active(char c)
{
    if (tsp) stack[tsp-1].active = c;
}


void
pop()
{
    if (tsp) --tsp;
}


int
peek()
{
    return tsp ? stack[tsp-1].column : 0 ;
}


void
dle()
{
    int i, xp, dsp;

    for ( xp = i = dsp = 0; dsp < tsp; dsp++ ) {
	while ( xp < stack[dsp].column ) {
	    ++xp;
	    putchar(' ');
	}
	putchar(stack[dsp].active);
	if ( stack[dsp].active == '`' )
	    stack[dsp].active = ' ';
    }
}


int
printjob(int first, int count, Proc *p, Proc *pp)
{
    int tind = 0;

    if ( showargs || !first ) dle();

    if ( count )
	tind = printf("-%d*[", 1+count);
    else if ( tsp )
	tind = printf("-");

    tind += printf("%s", p->process);
    if ( count )
	tind += printf("]");

    if ( showpid ) 
	tind += po() + printf("%d", p->pid);

    if ( showuser && pp && (p->uid != pp->uid) ) {
	struct passwd *pw = getpwuid(p->uid);

	tind += po();
	if ( pw )
	    tind += printf("%s", pw->pw_name);
	else
	    tind += printf("#%d", p->uid);
    }
    tind += pc();

    if ( showargs ) {
	if ( T(p->cmdline) ) {
	    unsigned int i, c;

	    putchar(' ');
	    for (i=0; i < S(p->cmdline); i++) {
		c = T(p->cmdline)[i];

		if ( c == 0 )
		    putchar(' ');
		else if ( c <= ' ' || !isprint(c) )
		    printf("\\\%03o", c);
		else
		    putchar(c);
	    }
	}
	putchar('\n');
    }
    else if ( p->child ) {
	putchar('-');
	putchar( p->child->sib ? '+' : '-');
	tind++;
    }
    return tind;
}


int
sameas(Proc *a, Proc *b, int walk)
{
    if ( ! (a && b) )
	return (a == b);

    if ( strcmp(a->process, b->process) != 0 )
	return 0;

    if ( !sameas(a->child, b->child, 1) )
	return 0;

    if ( !walk ) return 1;

    do {
	if ( !sameas(a->sib, b->sib, 0) )
	    return 0;
	a = a->sib;
	b = b->sib;
    } while ( a && b );
    return 1;
}


void
print(int indent, Proc *node, Proc *parent)
{
    int count = 0;
    int first = 1;

    if ( node == 0 ) {
	if ( !showargs ) putchar('\n');
	return;
    }
    if (sortme)
	node = sibsort(node);

    push(peek() + (showargs ? 2 : indent), '|');
    do {
	if ( compress && sameas(node, node->sib, 0) )
	    count++;
	else {
	    if ( !node->sib ) active('`');
	    print(printjob(first,count,node,parent),node->child, node);
	    count=first=0;
	}
    } while ( node = node->sib );
    pop();
}


void
userjobs(Proc *p, uid_t user)
{
    for ( ; p ; p = p->sib )
	if (p->uid == user)
	    print(printjob(1,0,p,0),p->child, p);
	else
	    userjobs(p->child, user);
}


main(int argc, char **argv)
{
    pid_t curid;
     
    Proc *init;
    Proc *newest;
    int opt;

    argv[0] = basename(argv[0]);

    opterr = 1;
    while ( (opt = getopt(argc, argv, "aclnpu")) != EOF )
	switch (opt) {
	case 'a': showargs = 1; compress = 0; break;
	case 'c': compress = 0; break;
	case 'l': clipping = 0; break;
	case 'n': sortme = compress = 0; break;
	case 'p': showpid  = 1; compress = 0; break;
	case 'u': showuser = 1; break;
	}
    init = ptree(showargs ? PTREE_ARGS : 0);

    argc -= optind;
    argv += optind;

    if (argc < 1)
	print(printjob(1,0,init,0),init->child, init);
    else if ( (curid = atoi(argv[0])) > 0 ) {
	if ( init = pfind(curid) )
	    print(printjob(1,0,init,0),init->child, init);
    }
    else {
	struct passwd *pwd;

	if ( !(pwd = getpwnam(argv[0])) ) {
	    fprintf(stderr, "No such user name: %s\n", argv[0]);
	    exit(1);
	}
	userjobs(init, pwd->pw_uid);
    }

    exit(0);
}
