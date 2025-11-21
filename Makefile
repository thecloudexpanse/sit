
all: sit macbinfilt

clean:
	rm -f sit macbinfilt
	rm -f *.o

sit: sit.o updcrc.o appledouble.o zopen.o
	$(CC) -o $@ $^

macbinfilt: macbinfilt.c
	$(CC) -o $@ $^
