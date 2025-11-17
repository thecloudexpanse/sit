
all: sit macbinfilt

clean:
	rm -f sit
	rm -f *.o

sit: sit.o updcrc.o appledouble.o
	$(CC) -o $@ $^
