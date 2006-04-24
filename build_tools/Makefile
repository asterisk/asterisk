MENUSELECT_OBJS=menuselect.o menuselect_curses.o
MENUSELECT_CFLAGS=-g -c -D_GNU_SOURCE -I../ -I../include/
MENUSELECT_LIBS=../mxml/libmxml.a $(CURSES_LIB)

menuselect: $(MENUSELECT_OBJS)
	$(CC) -g -o $@ $(MENUSELECT_OBJS) $(MENUSELECT_LIBS)

menuselect.o: menuselect.c menuselect.h
	$(CC) -o $@ $(MENUSELECT_CFLAGS) $<

menuselect_curses.o: menuselect_curses.c menuselect.h
	$(CC) -o $@ $(MENUSELECT_CFLAGS) $(CURSES_INCLUDE) $<

clean:
	rm -f menuselect *.o

dist-clean: clean
	rm -f menuselect-deps
