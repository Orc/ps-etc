#include <stdio.h>

#include "ptree.h"


int showargs = 0;
int compress = 1;
int clipping = 1;
int sortme   = 1;
int showpid  = 0;
int showuser = 0;

char col[1000];

int
cmp(Proc *a, Proc *b)
{
    int rc = strcasecmp(a->process,b->process);

    return rc ? rc : strcmp(a->process, b->process);
}


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

    if ( cmp(a,b) > 0 ) {
	p = t = a;
	a = a->sib;
    }
    else {
	p = t = b;
	b = b->sib;
    }

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


void
print(int indent, Proc *p, Proc *pp)
{
    int count = 1, first = 1;
    int tind, l0 = (indent == 0);
    Proc *w;

    if ( p == 0 ) {
	putchar('\n');
	return;
    }

    do {
	if ( compress && p->child == 0
		      && p->sib
		      && p->sib->child == 0
		      && strcmp(p->process, p->sib->process) == 0)
	    count++;
	else {
	    if ( first ) {
		if ( !l0 )
		    indent += printf("-");
		tind = indent;
	    }
	    else
		tind = printf("%*.*s",indent,indent,col);

	    if ( l0 ) {
		if ( count > 1 )
		    tind += printf("%d*[", count);
	    }
	    else {
		char *tic = p->sib ? (first ? '+' : '|')
				   : (first ? '-' : '*');
		col[indent] = '|';
		if ( count > 1 )
		    tind += printf("%c-%d*[", tic, count);
		else
		    tind += printf("%c-", tic);
	    }

	    tind += printf("%s", p->process);
	    if ( count > 1 )
		tind += printf("]");

	    if ( showpid )
		tind += printf("(%d)", p->pid);

#if 0
	    if ( showuser && pp && (p->uid != pp->uid) )
#endif

	    p->child = sibsort(p->child);
	    print(tind, p->child, p);
	    col[indent] = ' ';
	    count = 1;
	    first = 0;
	}
    } while ( p = p->sib );
}


main(int argc, char **argv)
{
    pid_t curid;
     
    Proc *init = ptree();
    Proc *cur;
    Proc *newest;
    int opt;

    argv[0] = basename(argv[0]);

    opterr = 1;
    while ( (opt = getopt(argc, argv, "aclnpu")) != EOF )
	switch (opt) {
	case 'a': showargs = 1; break;
	case 'c': compress = 0; break;
	case 'l': clipping = 0; break;
	case 'n': sortme = compress = 0; break;
	case 'p': showpid  = 1; compress = 0; break;
	case 'u': showuser = 1; break;
	}

    argc -= optind;
    argv += optind;

    memset(col, ' ', sizeof col);

    if (argc < 1)
	print(0, init, 0);
    else if ( (curid = atoi(argv[0])) > 0 ) {
	if ( cur = pfind(curid) )
	    print(0, cur);
    }
    exit(0);
}
