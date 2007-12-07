typedef struct {
    char *text;
    int size,alloc;
} Cstring;

#define CREATE(x)	(x).text = malloc( ( ((x).size=0),((x).alloc=100)) )
#define EXPAND(x)	(x).text[((x).size < (x).alloc \
			    ? 0 \
			    : !((x).text = realloc(x.text, x.alloc += 100))), \
			(x).size++]

#define T(x)		(x).text
#define S(x)		(x).size
