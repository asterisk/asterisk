
CC=gcc
#CFLAGS=-Wall
INCLUDE=-I../include

all: localtime.o

clean::
	rm -f localtime.o test

depend::
	@echo "Nothing to do for depend"

test: test.c
	${CC} ${CFLAGS} -o test test.c

localtime.o: localtime.c


