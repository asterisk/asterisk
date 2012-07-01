#Modify this to point to the PJSIP location.
PJBASE=~/Desktop/project/pjproject

include $(PJBASE)/build.mak

CC      = $(APP_CC)
LDFLAGS = $(APP_LDFLAGS)
LDLIBS  = $(APP_LDLIBS)
CFLAGS  = $(APP_CFLAGS)
CPPFLAGS= ${CFLAGS}

# If your application is in a file named myapp.cpp or myapp.c
# # this is the line you will need to build the binary.
# all: myapp
#
cmp_wav: cmp_wav.c
	$(CC) -o $@ $< $(CPPFLAGS) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f cmp_wav.o cmp_wav

