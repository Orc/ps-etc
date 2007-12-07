
pstree: pstree.o ptree.o
	$(CC) -o pstree pstree.o ptree.o

clean:
	rm -f pstree *.o
