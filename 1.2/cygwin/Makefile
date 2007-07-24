OBJS=cygloader.o
all: $(OBJS)
$(OBJS) : %.o: %.c
	$(CC) $< -o asterisk.exe
clean:
	rm -f asterisk.exe
