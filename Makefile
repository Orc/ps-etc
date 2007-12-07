CC=cc -baout

pstree: pstree.o ptree.o

clean:
	rm -f pstree *.o
