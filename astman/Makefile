
TARGET=$(shell if [ -f /usr/include/newt.h ]; then echo "astman"; fi)
all: $(TARGET)

install:
	mkdir -p /usr/sbin
	if [ "$(TARGET)" != "" ]; then \
		for x in $(TARGET); do \
			install -m 755 $$x /usr/sbin/astman; \
		done ; \
	fi

clean:
	rm -f *.o astman

astman: astman.o
	$(CC) -o astman astman.o -lnewt
