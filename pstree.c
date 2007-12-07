/*
 * pstree: display a process heirarchy.
 */
#include "config.h"

#include <stdio.h>
#include <pwd.h>
#include <sys/types.h>

#include <unistd.h>
#if HAVE_LIBGEN_H
# include <libgen.h>
#endif

#include "ptree.h"

#if !HAVE_BASENAME
#include <string.h>

char *
basename(char *p)
{
    char *ret = strrchr(p, '/');

    return ret ? ret+1 : p;
}
#endif


int showargs = 0;	/* -a:  show the entire command line */
int compress = 1;	/* !-c: compact duplicate subtrees */
int clipping = 1;	/* !-l: clip output to screenwidth */
int sortme   = 1;	/* !-n: sort output */
int showpid  = 0;	/* -p:  show process ids */
int showuser = 0;	/* -u:  show username transitions */


/* do a more macosish sort;  try to not pay attention to case when sorting.
 */
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


/* sort the sibling list by ripping into two substrings, sorting
 * each substring, then stitching them together with a merge sort.
 */
Proc *
sibsort(Proc *p)
{
    Proc *d, *tail, *left = 0, *right = 0;
    int even = 0;

    if ( !(p && p->sib) ) return p;

    /* split into two lists */
    while (p) {
	d = p;
	p = p->sib;

	if (even) {
	    d->sib = left;
	    left = d;
	}
	else {
	    d->sib = right;
	    right = d;
	}
	even = !even;
    }

    /* sort them */
    if ( left ) left = sibsort(left);
    if ( right ) right = sibsort(right);

    /* merge them together */
    for ( p = tail = 0; left && right;  ) {
	if (cmp(left,right) < 0) {
	    d = left;
	    left = left->sib;
	}
	else {
	    d = right;
	    right = right->sib;
	}
	if ( p )
	    tail->sib = d;
	else
	    p = d;
	tail = d;
    }
    tail->sib = left ? left : right;

    return p;
}


/* fancy output printing:  po() and pc() are for
 * comma-delimited ()'ed strings.
 */
static int _paren = 0;

/* when first called, po() prints '(', then every other time
 * it's called it prints ',' until pc() is called to close
 * the parentheses.
 */
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


/* to keep track of downward branches, we stuff (column,downward arrow)
 * a tabstack and have dle() properly expand them into spaces, '|', and
 * '`'s
 */
struct tabstack {
    int column;
    int active;
} stack[100];

int tsp = 0;


/* shove a branch onto the tabstack.
 */
void
push(int column, int c)
{
    stack[tsp].column = column;
    stack[tsp].active = c;
    ++tsp;
}


/* set the downward arrow at tos.
 */
void
active(char c)
{
    if (tsp) stack[tsp-1].active = c;
}


/* pop() [and discard] tos
 */
void
pop()
{
    if (tsp) --tsp;
}


/* return the column position at tos, or 0 if the stack is empty.
 */
int
peek()
{
    return tsp ? stack[tsp-1].column : 0 ;
}


/* print whitespace and downward branches at the start of a line.
 * '`' is a specialcase downward branch;  it's where a branch turns
 * towards the end of the line, so when dle() prints it it resets it
 * to ' '
 */
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


/* print process information (process name, id, uid translation)
 * and branch prefixes and suffixes.   Returns the # of characters
 * printed, so print() can adjust the indent for subtrees
 */
int
printjob(int first, int count, Proc *p)
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

    if ( showuser && p->parent && (p->uid != p->parent->uid) ) {
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


/* compare two process trees (for subtree compaction)
 * process trees are identical if
 *     (a) the processes are the same
 *     (b) their ->child trees are identical
 *     (c) [if required] all of their siblings are identical
 */
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


/* print() a subtree, indented by a header.
 */
void
print(int indent, Proc *node)
{
    int count = 0;
    int first = 1;
    int sibs;

    if ( node == 0 ) {
	if ( !showargs ) putchar('\n');
	return;
    }
    if (sortme)
	node = sibsort(node);

    sibs = node->sib != 0;
    push(peek() + (showargs ? 2 : indent), sibs ? '|' : ' ');
    do {
	if ( compress && sameas(node, node->sib, 0) )
	    count++;
	else {
	    if ( sibs && !node->sib ) active('`');
	    print(printjob(first,count,node),node->child);
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
	    print(printjob(1,0,p),p->child);
	else
	    userjobs(p->child, user);
}


main(int argc, char **argv)
{
    pid_t curid;
    Proc *init;
    Proc *newest;
    int opt;
    char *e;
    extern char version[];

    argv[0] = basename(argv[0]);

    opterr = 1;
    while ( (opt = getopt(argc, argv, "aclnpuV")) != EOF )
	switch (opt) {
	case 'a': showargs = 1; compress = 0; break;
	case 'c': compress = 0; break;
	case 'l': clipping = 0; break;
	case 'n': sortme = compress = 0; break;
	case 'p': showpid  = 1; compress = 0; break;
	case 'u': showuser = 1; break;
	case 'V': printf("%s (ps-etc) %s\n", argv[0], version); exit(0);
	default : exit(1);
	}
    init = ptree(showargs ? PTREE_ARGS : 0);

    argc -= optind;
    argv += optind;

    if ( argc < 1 ) {
	print(printjob(1,0,init),init->child);
	exit(0);
    }

    curid = strtol(argv[0], &e, 10);

    if ( *e == 0 ) {
	if ( init = pfind(curid) )
	    print(printjob(1,0,init),init->child);
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
