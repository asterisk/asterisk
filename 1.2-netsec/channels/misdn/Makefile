#
# Makefile
#
# Make file for chan_misdn support
#

# Verify those options with main Makefile
CFLAGS		+= -pipe -c -DMISDNUSER_JOLLY
SOURCES		= isdn_lib.c isdn_msg_parser.c 
OBJDIR		= .
OBJS		= isdn_lib.o isdn_msg_parser.o


all: chan_misdn_lib.a 


%.o: %.c
	$(CC) $(CFLAGS) -o $@ $<


chan_misdn_lib.a:	$(OBJS)
	ar crv $@ $(OBJS)

misdn: test_preempt
	if [ ! -d lib ] ; then \
		mkdir lib; \
		cd lib ; \
		wget http://isdn.jolly.de/download/v3.1/mISDN_for_PBX4Linux-3.0.tar.gz ;\
		tar xzf mISDN_for_PBX4Linux-3.0.tar.gz; \
		wget http://isdn.jolly.de/download/v3.1/mISDNuser_for_PBX4Linux-3.0.tar.gz ;\
		tar xzf mISDNuser_for_PBX4Linux-3.0.tar.gz ;\
		cd mISDN; patch -p1 <../../mISDN.patch; \
		cd ../mISDNuser ; patch -p1 <../../mISDNuser.patch; \
	fi
	cd lib/mISDN ; make install
	cd lib/mISDNuser ; make install

LINUX=/lib/modules/$(uname -r)/build
GCCVERSION=$(shell $(CC) --version  | grep GCC  | cut -d " " -f 3  | cut -d "." -f 1)

test_preempt:
	@if  grep 'CONFIG_DEBUG_SPINLOCK=y' $(LINUX)/.config   ; then \
		echo -e "\n\n!!!!!!!!!!!!!!!!!!!!!!!!!!\nDisable the DEBUG_SPINLOCK Setting in your Kernel Config.\n with this option set, mISDN will not work! \n\n" ;\
		read ; \
		exit 1 ; \
	fi
	@if  grep 'CONFIG_DEBUG_SPINLOCK_SLEEP=y' $(LINUX)/.config   ; then \
		echo -e "\n\n!!!!!!!!!!!!!!!!!!!!!!!!!!\nDisable the DEBUG_SPINLOCK_SLEEP Setting in your Kernel Config.\n with this option set, mISDN will not work! \n\n" ;\
		read ; \
		exit 1 ; \
	fi
	@if  grep 'CONFIG_SMP=y' $(LINUX)/.config   ; then \
		echo -e "\n\n!!!!!!!!!!!!!!!!!!!!!!!!!!\nDisable the SMP Setting in your Kernel Config.\n\n" ; \
		read ; \
		exit 1 ; \
	fi
	@if test "$(GCCVERSION)" -gt 3 ; then \
		echo -e "\n\n!!!!!!!!!!!!!!!!!!!!!!!!!!\nYou're using GCC 4! Please downgrade to gcc-3.x and type:\nexport CC=gcc-3.x\nbefore issuing make again.\nyou won't have success with gcc-4!\n\n" ; \
		read ; \
		exit 1 ; \
	fi



FORCE:

clean: 
	rm -rf *.a *.o *.so

misdn_clean:
	rm -rf lib

