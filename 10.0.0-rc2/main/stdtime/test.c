/*! \file 
  \brief Testing localtime functionality */

#include "localtime.c"
#include <sys/time.h>
#include <stdio.h>

int main(int argc, char **argv) {
	struct timeval tv;
	struct tm	tm;
	char	*zone[4] = { "America/New_York", "America/Chicago", "America/Denver", "America/Los_Angeles" };
	int	i;

	gettimeofday(&tv,NULL);

	for (i=0;i<4;i++) {
		ast_localtime(&tv.tv_sec,&tm,zone[i]);
		printf("Localtime at %s is %04d/%02d/%02d %02d:%02d:%02d\n",zone[i],tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	}
	return 0;
}
