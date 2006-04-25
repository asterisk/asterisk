MENUSELECT_OBJS=menuselect.o menuselect_curses.o ../strcompat.o
MENUSELECT_CFLAGS=-g -c -D_GNU_SOURCE -I../ -I../include/
MENUSELECT_LIBS=../mxml/libmxml.a

ifneq ($(NCURSES_LIB),)
  MENUSELECT_LIBS+=$(NCURSES_LIB)
  MENUSELECT_INCLUDE=$(NCURSES_INCLUDE)
else
  MENUSELECT_LIBS+=$(CURSES_LIB)
  MENUSELECT_INCLUDE=$(CURSES_INCLUDE)
endif

menuselect: $(MENUSELECT_OBJS)
	$(CC) -g -o $@ $(MENUSELECT_OBJS) $(MENUSELECT_LIBS)

menuselect.o: menuselect.c menuselect.h
	$(CC) -o $@ $(MENUSELECT_CFLAGS) $<

menuselect_curses.o: menuselect_curses.c menuselect.h
	$(CC) -o $@ $(MENUSELECT_CFLAGS) $(MENUSELECT_INCLUDE) $<

clean:
	rm -f menuselect *.o

dist-clean: clean
	rm -f menuselect-deps
