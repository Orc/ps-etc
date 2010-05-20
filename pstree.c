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


extern int  printcard(char *,...);
extern void ejectcard();
extern void cardwidth();

int showargs = 0;	/* -a:  show the entire command line */
int compress = 1;	/* !-c: compact duplicate subtrees */
int clipping = 1;	/* !-l: clip output to screenwidth */
int sortme   = 1;	/* !-n: sort output */
int showpid  = 0;	/* -p:  show process ids */
int showuser = 0;	/* -u:  show username transitions */
int exposeargs = 0;	/* -s:  expand spaces inside arguments to \040 */

Proc * sibsort(Proc *);

/* compare two ->child trees
 */
cmpchild(Proc *a, Proc *b)
{
    int rc;

    if ( a && b ) {
	if ( a->children != b->children )
	    return a->children - b->children;

	if ( !a->sorted ) {
	    a->child = sibsort(a->child);
	    a->sorted = 1;
	}
	if ( !b->sorted ) {
	    b->child = sibsort(b->child);
	    b->sorted = 1;
	}

	a = a->child;
	b = b->child;

	while ( a && b ) {
	    if ( rc = cmp(a,b) )
		return rc;
	    a = a->sib;
	    b = b->sib;
	}
    }
    if ( a == b) return 0;
    else if ( a ) return 1;
    else return -1;
}


/* do a more macosish sort;  try to not pay attention to case when sorting.
 */
int
cmp(Proc *a, Proc *b)
{
    int rc = strcasecmp(a->process, b->process);

    if ( rc == 0 )
	rc = strcmp(a->process, b->process);

    if ( rc == 0 )
	rc = cmpchild(a, b);

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

/* when first called, po() prints ' (', then every other time
 * it's called it prints ',' until pc() is called to close
 * the parentheses.
 */
int
po()
{
    if (_paren)
	return putcard(',');
    _paren = 1;
    return putcard(' ') + putcard('(');
}


int
pc()
{
    if (_paren) {
	_paren = 0;
	return putcard(')');
    }
    return 0;
}


static int _bracket = 0;

void
bo()
{
    _bracket++;
}

void
bc()
{
    if (_bracket) --_bracket;
}

void
eol()
{
    int i;

    for (i=0; i < _bracket; i++)
	putcard(']');
    ejectcard();
}


/* to keep track of downward branches, we stuff (column,downward arrow)
 * a tabstack and have dle() properly expand them into spaces, '|', and
 * '`'s
 */
typedef struct tabstack {
    int column;
    int active;
} TabStack;

STRING(TabStack) stack;

int tsp = 0;


/* shove a branch onto the tabstack.
 */
void
push(int column, int c)
{
    if ( tsp >= S(stack) )
	EXPAND(stack);

    T(stack)[tsp].column = column;
    T(stack)[tsp].active = c;
    ++tsp;
}


/* set the downward arrow at tos.
 */
void
active(char c)
{
    if (tsp) T(stack)[tsp-1].active = c;
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
    return tsp ? T(stack)[tsp-1].column : 0 ;
}


/* print whitespace and downward branches at the start of a line.
 * '`' is a specialcase downward branch;  it's where a branch turns
 * towards the end of the line, so when dle() prints it it resets it
 * to ' '
 */
void
dle()
{
    int dx, xp, dsp;
    char c;

    for ( xp = dsp = 0; dsp < tsp; dsp++ ) {
	dx = T(stack)[dsp].column - xp;
	printcard("%*s%c", dx, "", T(stack)[dsp].active);
	xp = T(stack)[dsp].column;
	if ( T(stack)[dsp].active == '`' )
	    T(stack)[dsp].active = ' ';
    }
}


int
printchar(char c)
{
    if ( c == 0 )
	return putcard(' ');
    else if ( c == ' ' && exposeargs )
	return printcard("\\\%03o", c);
    else if ( c < ' ' || !isprint(c) )
	return printcard("\\\%03o", c);
    else
	return putcard(c);
}


int
printargv0(char *p)
{
    int ret = 0;

    while ( *p )
	ret += printchar(*p++);
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
	tind = printcard("-%d*[", 1+count);
    else if ( tsp )
	tind = putcard('-');

    if ( showargs ) {
	if ( T(p->cmdline) ) {
	    unsigned int i;

	    tind += printargv0( (clipping && !p->renamed) ? basename(T(p->cmdline)) : T(p->cmdline) );
		
	    for (i=0; i < S(p->cmdline) && T(p->cmdline)[i]; i++)
		;
	    
	    for (; i < S(p->cmdline); i++)
		printchar(T(p->cmdline)[i]);
	}
	if ( p->renamed )
	    po() + printcard("%s", p->process) + pc();
    }
    else
	tind += printcard("%s", p->process);

    if ( showpid )
	tind += po() + printcard("%d", p->pid);

    if ( showuser && p->parent && (p->uid != p->parent->uid) ) {
	struct passwd *pw = getpwuid(p->uid);

	tind += po();
	if ( pw )
	    tind += printcard("%s", pw->pw_name);
	else
	    tind += printcard("#%d", p->uid);
    }

    tind += pc();

    if ( showargs )
	eol();
    else if ( p->child ) {
	putcard('-');
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

    if ( a->pid == b->pid )
	return 1;

    if ( strcmp(a->process, b->process) != 0 )
	return 0;

    if ( showargs ) {
	if ( a->renamed != b->renamed )
	    return 0;

	if ( S(a->cmdline) != S(b->cmdline) )
	    return 0;
	if ( memcmp(T(a->cmdline), T(b->cmdline), S(a->cmdline)) )
	    return 0;
    }

    if ( !sameas(a->child, b->child, 1) )
	return 0;

    if ( walk ) do {
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
print(int first, int count, Proc *node)
{
    int indent;
    char branch;

    indent = printjob(first,count,node);

    if ( node->child ) {
	if ( sortme && !node->sorted ) {
	    node->child = sibsort(node->child);
	    node->sorted = 1;
	}
	node = node->child;
    }
    else {
	if ( !showargs ) eol();
	return;
    }

    count = 0;
    first = 1;

    do {
	if ( compress && sameas(node, node->sib, 0) )
	    count++;
	else {
	    if ( first ) {
		if ( !showargs )
		    putcard(node->sib ? '+' : '-' );
		branch = node->sib ? '|' : ' ';
		push(peek() + (showargs ? 2 : indent), branch);
	    }
	    if ( branch != ' ' && !node->sib) active('`');
	    if ( count ) bo();
	    print(first,count,node);
	    if ( count ) bc();
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
	    print(1,0,p);
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
    while ( (opt = getopt(argc, argv, "aclnpsuV")) != EOF )
	switch (opt) {
	case 'a':   showargs = 1; break;
	case 'c':   compress = 0; break;
	case 'l':   clipping = 0; break;
	case 'n':   sortme = compress = 0; break;
	case 'p':   showpid  = 1; break;
	case 's':   exposeargs = 1; break;
	case 'u':   showuser = 1; break;
	case 'V':   printf("%s (ps-etc) %s\n", argv[0], version); exit(0);
	default :   exit(1);
	}

    init = ptree(showargs ? PTREE_ARGS : 0);

    if ( !init ) {
	perror(argv[0]);
	exit(1);
    }

    if ( clipping )
	cardwidth();

    argc -= optind;
    argv += optind;

    if ( argc < 1 ) {
	print(1,0,init);
	exit(0);
    }

    curid = strtol(argv[0], &e, 10);

    if ( *e == 0 ) {
	if ( init = pfind(curid) )
	    print(1,0,init);
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
