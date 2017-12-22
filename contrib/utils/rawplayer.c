/*
  Rawplayer.c simple raw file stdout player
  (c) Anthony C Minessale II <anthmct@yahoo.com>

  2006-03-10: Bruno Rocha <bruno@3gnt.net>
  - include <stdlib.h> to remove compiler warning on some platforms
  - check for read/write errors (avoid 100% CPU usage in some asterisk failures)
*/

#define BUFLEN 320
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

static int deliver_file(char *path, int fdout) {
	int fd = 0, bytes = 0, error = 0;
	short buf[BUFLEN];

	if ((fd = open(path,O_RDONLY))) {
		while ((bytes=read(fd, buf, BUFLEN)) > 0) {
			if(write(fdout, buf, bytes) < 0){
				error = -2;
				break;
			}
		}
		if(fd)
			close(fd);
	} else
		return -1;

	return error;
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
