/** @file pbx_wilcalu.c 
 *
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

 *  Autodialer for Asterisk 
 *  Redirect dialstring thru fifo "/var/run/autodial.ctl"
 *  Format of string is :
 *  "tech/tele,filename&" ie. "tor1/23,file&"
 */
 
#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>


// Globals
const   char *dialfile="/var/run/autodial.ctl";
static  char *tdesc = "Wil Cal U (Auto Dialer)";
static  pthread_t autodialer_thread;
static  char buf[257];
static  char lastbuf[257];//contains last partial buffer
static  char sendbuf[257];
extern  int errno;
STANDARD_LOCAL_USER;
LOCAL_USER_DECL;

//prototype
static void *dialstring(void *string);

// types
struct alarm_data {
time_t alarm_time;
int    snooze_len;
void   *dialstr;
};

static void *autodial(void *ignore)
{
	pthread_t dialstring_thread;
	char * sendbufptr=sendbuf;
	int fd=open(dialfile,O_RDONLY);
	printf("Entered Wil-Calu fd=%d\n",fd);
	if(fd<0) {
		printf("Autodial: Unable to open file\n");
		pthread_exit(NULL);
	}
	memset(buf,0,257);
	memset(lastbuf,0,257);
	memset(sendbuf,0,257);
	while(1){
		ssize_t bytes;
		void *pass;

		memset(buf,0,257);
		bytes=read(fd,buf,256);
		buf[(int)bytes]=0;

		if(bytes){
			int x;
			printf("WilCalu : Read Buf %s\n",buf);
			sendbufptr=sendbuf;
			for(x=0;lastbuf[x]!=0 && x<257;x++);
			if(x) {
				memcpy(sendbuf,lastbuf,x);
				sendbufptr+=x;
				memset(lastbuf,0,257);
			}
			/* Process bytes read */
			for(x=0;x<bytes;x++){
				/* if & then string is complete */
				if(buf[x]=='&'){
					if(NULL!=(pass=(void *)strdup(sendbuf))){
						pthread_create(&dialstring_thread,NULL,dialstring,pass);
						sendbufptr=sendbuf;
						memset(sendbuf,0,257);
					}
					else {
						perror("Autodial:Strdup failed");
						close(fd);
						pthread_exit(NULL);
					}
				} else {
					if(buf[x]=='\n')
						continue;
					*sendbufptr=buf[x];
					sendbufptr++;
					*sendbufptr=0;
				}
			}
			if(sendbufptr!=sendbuf)
				memcpy(lastbuf,sendbuf,sendbufptr-sendbuf+1);
		}
	}
	close(fd);
	pthread_exit(NULL);
	return NULL;
}

static void *snooze_alarm(void *pass){
	
	pthread_t dialstring_thread;
	struct alarm_data *data=(struct alarm_data *)pass;
	sleep(data->snooze_len);
	pthread_create(&dialstring_thread,NULL,dialstring,data->dialstr);
	// dialstring will free data->dialstr
	free(pass);
	pthread_exit(NULL);
	return NULL;
}
static void  set_snooze_alarm(char *dialstr,int snooze_len){
	pthread_t snooze_alarm_thread;
	struct alarm_data *pass;
	printf("Answered: Snooze Requested\n");
	if(NULL==(pass=malloc(sizeof(struct alarm_data)))){
		perror("snooze_alarm: data");
		pthread_exit(NULL);
	}
	if(NULL==(pass->dialstr=(void *)strdup(dialstr))){
		free(pass);
		perror("snooze_alarm: dialstr");
		pthread_exit(NULL);
	}
	pass->snooze_len=snooze_len;
	pthread_create(&snooze_alarm_thread,NULL,snooze_alarm,pass);
}
			
static void *dialstring(void *string){
	struct ast_channel *channel;
	char *bufptr,*destptr;
	// ms affects number of rings
	int  ms=10000;
	int  cnt=0,first;
	char tech[256];
	char tele[256];
	char filename[256];
	int  answered=0;
	for(first=0,bufptr=(char *)string,destptr=tech;*bufptr&&cnt<256;cnt++){
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
	} 
	*destptr=0;
	printf("Printing string arg: ");
	printf((char *)string);
	printf(" Eos\n");
	if(strlen(tech)+strlen(tele)+strlen(filename)>256){
		printf("Autodial:Error string too long\n");
		free(string);
		pthread_exit(NULL);
	}
	printf("Autodial Tech %s(%d) Tele %s(%d) Filename %s(%d)\n",tech,strlen(tech),tele,strlen(tele),filename,strlen(filename));

	channel=ast_request(tech,AST_FORMAT_SLINEAR,tele);
	if(channel!=NULL){
		ast_call(channel,tele,10000);
	}
	else {
		printf("Autodial:Sorry unable to obtain channel\n");
		free(string);
		pthread_exit(NULL);
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
					char res;
					ast_streamfile(channel,filename,0);
					// Press Five for snooze
					res=ast_waitstream(channel, "37");
					if(res=='3'){
						answered=1;
						set_snooze_alarm((char *)string,60);
						ast_streamfile(channel,"demo-thanks",0);
						ast_waitstream(channel, "");
					}
					else if(res=='7'){
						answered=1;
						ast_streamfile(channel,"demo-thanks",0);
						ast_waitstream(channel, "");
					}
					ast_stopstream(channel);
					ms=0;
				}
			}
			else if(f->subclass==AST_CONTROL_RINGING)
				printf("Autodial:Phone Ringing end\n");
		}
		ast_frfree(f);
	}
	if(!answered)
		set_snooze_alarm((char *)string,5);
	free(string);
	ast_hangup(channel);
	printf("Autodial:Hung up channel\n");
	pthread_exit(NULL);
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
	if((val=mkfifo(dialfile, 0700))){
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
