
TARGET=$(shell if [ -f /usr/include/newt.h ]; then echo "astman"; else echo "none" ; fi)
all: $(TARGET)

install:
	if [ "$(TARGET)" != "none" ]; then \
		for x in $(TARGET); do \
			install -m 755 $$x $(ASTSBINDIR)/astman; \
		done ; \
	fi

none:
	@echo Not building the Asterisk Manager "astman"

clean:
	rm -f *.o astman

astman: astman.o
	$(CC) -o astman astman.o -lnewt
