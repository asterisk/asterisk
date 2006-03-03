/*
  Rawplayer.c simple raw file stdout player
  (c) Anthony C Minessale II <anthmct@yahoo.com>
*/

#define BUFLEN 320
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static int deliver_file(char *path, int fdout) {
	int fd = 0, bytes = 0;
	short buf[BUFLEN];

	if ((fd = open(path,O_RDONLY))) {
		while ((bytes=read(fd, buf, BUFLEN))) {
			write(fdout, buf, bytes);
		}
		if(fd)
			close(fd);
	} else 
		return -1;
	
	return 0;
}


int main(int argc, char *argv[]) {
	int x = 0, fdout = 0;
	fdout = fileno(stdout);
	for (;;)
		for (x = 1; x < argc ; x++) {
			if(deliver_file(argv[x], fdout))
				exit(1);
		}
}

