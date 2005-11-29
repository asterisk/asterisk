/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Trivial application to playback a sound file
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

const   char *dialfile="/var/run/autodial.ctl";
static  char *tdesc = "Wil Cal U (Auto Dialer)";
static  pthread_t autodialer_thread;
static  char buf[256];
extern  int errno;
STANDARD_LOCAL_USER;
LOCAL_USER_DECL;

static void *autodial(void *ignore)
{
	while(1){
		struct ast_channel *channel;
		int fd=open(dialfile,O_RDONLY);
		char *bufptr,*destptr;
		int  ms=10000;
		int  cnt=0,first;
		char tech[256];
		char tele[256];
		char filename[256];

		if(fd<0) {
			printf("Autodial: Unable to open file\n");
			pthread_exit(NULL);
		}
		memset(buf,0,256);
		read(fd,buf,256);
		for(first=0,bufptr=buf,destptr=tech;*bufptr&&cnt<256;cnt++){
			if(*bufptr=='/' && !first){
				*destptr=0;
				destptr=tele;
				first=1;
			}
			else if(*bufptr==','){
				*destptr=0;
				destptr=filename;
			} else {
				*destptr=*bufptr;
				destptr++;
			}
			bufptr++;
		} destptr--;*destptr=0;
		if(strlen(tech)+strlen(tele)+strlen(filename)>256){
			printf("Autodial:Error string Error too long\n");
			pthread_exit(NULL);
		}
			
#if 0
		printf("Autodial Tech %s(%d) Tele %s(%d) Filename %s(%d)\n",tech,strlen(tech),tele,strlen(tele),filename,strlen(filename));
#endif
		channel=ast_request(tech,AST_FORMAT_SLINEAR,tele);
		if(channel!=NULL){
			ast_call(channel,tele,10000);
		}
		else {
			printf("Autodial:Sorry unable to obtain channel\n");
			continue;
		}
		if(channel->state==AST_STATE_UP)
			printf("Autodial:Line is Up\n");
		while(ms>0){
			struct ast_frame *f;
			ms=ast_waitfor(channel,ms);
			f=ast_read(channel);
			if(!f){
				printf("Autodial:Hung Up\n");
				break;
			}
			if(f->frametype==AST_FRAME_CONTROL){
				if(f->subclass==AST_CONTROL_ANSWER){
					printf("Autodial:Phone Answered\n");
					if(channel->state==AST_STATE_UP){
						ast_streamfile(channel,filename,0);
						ast_waitstream(channel, "");
						ast_stopstream(channel);
						ms=0;
					}
				}
				else if(f->subclass==AST_CONTROL_RINGING)
					printf("Autodial:Phone Ringing end\n");
			}
			ast_frfree(f);
		}
		ast_hangup(channel);
		printf("Autodial:Hung up channel\n");
		close(fd);
	}
	// never reached
	return NULL;
}
int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	unlink(dialfile);
	return 0;
}

int load_module(void)
{
	int val;
	if((val=mkfifo(dialfile,O_RDWR))){
		if(errno!=EEXIST){
			printf("Error:%d Creating Autodial FIFO\n",errno);
			return 0;
		}
	}
	pthread_create(&autodialer_thread,NULL,autodial,NULL);
	return 0;
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
