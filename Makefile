
all:	sit macbinfilt

sit:	sit.o updcrc.o
	cc -o sit sit.o updcrc.o

clean:
	rm sit.o updcrc.o sit macbinfilt
