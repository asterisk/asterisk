BINARIES=parse create
CFLAGS=-Wall -ggdb -g3 -I..
LDFLAGS=-L..
LIBS=-lmmime
CC=gcc

all: parse create

parse: parse.o
	$(CC) -o parse parse.o $(LDFLAGS) $(LIBS)

create: create.o
	$(CC) -o create create.o $(LDFLAGS) $(LIBS)

clean:
	rm -f $(BINARIES)
	rm -f *.o
	rm -f *.core
