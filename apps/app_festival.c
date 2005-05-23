/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Connect to festival
 * 
 * Copyright (C) 2002, Christos Ricudis
 *
 * Christos Ricudis <ricudis@itc.auth.gr>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <sys/types.h>
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/md5.h"
#include "asterisk/config.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>


#define FESTIVAL_CONFIG "festival.conf"

static char *tdesc = "Simple Festival Interface";

static char *app = "Festival";

static char *synopsis = "Say text to the user";

static char *descrip = 
"  Festival(text[|intkeys]):  Connect to Festival, send the argument, get back the waveform,"
"play it to the user, allowing any given interrupt keys to immediately terminate and return\n"
"the value, or 'any' to allow any number back (useful in dialplan)\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static char *socket_receive_file_to_buff(int fd,int *size)
{
    /* Receive file (probably a waveform file) from socket using   */
    /* Festival key stuff technique, but long winded I know, sorry */
    /* but will receive any file without closeing the stream or    */
    /* using OOB data                                              */
    static char *file_stuff_key = "ft_StUfF_key"; /* must == Festival's key */
    char *buff;
    int bufflen;
    int n,k,i;
    char c;

    bufflen = 1024;
    buff = (char *)malloc(bufflen);
    *size=0;

    for (k=0; file_stuff_key[k] != '\0';)
    {
        n = read(fd,&c,1);
        if (n==0) break;  /* hit stream eof before end of file */
        if ((*size)+k+1 >= bufflen)
        {   /* +1 so you can add a NULL if you want */
            bufflen += bufflen/4;
            buff = (char *)realloc(buff,bufflen);
        }
        if (file_stuff_key[k] == c)
            k++;
        else if ((c == 'X') && (file_stuff_key[k+1] == '\0'))
        {   /* It looked like the key but wasn't */
            for (i=0; i < k; i++,(*size)++)
                buff[*size] = file_stuff_key[i];
            k=0;
            /* omit the stuffed 'X' */
        }
        else
        {
            for (i=0; i < k; i++,(*size)++)
                buff[*size] = file_stuff_key[i];
            k=0;
            buff[*size] = c;
            (*size)++;
        }

    }

    return buff;
}

static int send_waveform_to_fd(char *waveform, int length, int fd) {

        int res;
        int x;
#ifdef __PPC__ 
	char c;
#endif

        res = fork();
        if (res < 0)
                ast_log(LOG_WARNING, "Fork failed\n");
        if (res)
                return res;
        for (x=0;x<256;x++) {
                if (x != fd)
                        close(x);
        }
/*IAS */
#ifdef __PPC__  
	for( x=0; x<length; x+=2)
	{
		c = *(waveform+x+1);
		*(waveform+x+1)=*(waveform+x);
		*(waveform+x)=c;
	}
#endif
	
	write(fd,waveform,length);
	close(fd);
	exit(0);
}


static int send_waveform_to_channel(struct ast_channel *chan, char *waveform, int length, char *intkeys) {
	int res=0;
	int fds[2];
	int ms = -1;
	int pid = -1;
	int needed = 0;
	int owriteformat;
	struct ast_frame *f;
	struct myframe {
		struct ast_frame f;
		char offset[AST_FRIENDLY_OFFSET];
		char frdata[2048];
	} myf;
	
        if (pipe(fds)) {
                 ast_log(LOG_WARNING, "Unable to create pipe\n");
        	return -1;
        }
	                                                
	/* Answer if it's not already going */
	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);
	ast_stopstream(chan);

	owriteformat = chan->writeformat;
	res = ast_set_write_format(chan, AST_FORMAT_SLINEAR);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set write format to signed linear\n");
		return -1;
	}
	
	res=send_waveform_to_fd(waveform,length,fds[1]);
	if (res >= 0) {
		pid = res;
		/* Order is important -- there's almost always going to be mp3...  we want to prioritize the
		   user */
		for (;;) {
			ms = 1000;
			res = ast_waitfor(chan, ms);
			if (res < 1) {
				res = -1;
				break;
			}
			f = ast_read(chan);
			if (!f) {
				ast_log(LOG_WARNING, "Null frame == hangup() detected\n");
				res = -1;
				break;
			}
			if (f->frametype == AST_FRAME_DTMF) {
				ast_log(LOG_DEBUG, "User pressed a key\n");
				if (intkeys && strchr(intkeys, f->subclass)) {
					res = f->subclass;
					ast_frfree(f);
					break;
				}
			}
			if (f->frametype == AST_FRAME_VOICE) {
				/* Treat as a generator */
				needed = f->samples * 2;
				if (needed > sizeof(myf.frdata)) {
					ast_log(LOG_WARNING, "Only able to deliver %d of %d requested samples\n",
						(int)sizeof(myf.frdata) / 2, needed/2);
					needed = sizeof(myf.frdata);
				}
				res = read(fds[0], myf.frdata, needed);
				if (res > 0) {
					myf.f.frametype = AST_FRAME_VOICE;
					myf.f.subclass = AST_FORMAT_SLINEAR;
					myf.f.datalen = res;
					myf.f.samples = res / 2;
					myf.f.mallocd = 0;
					myf.f.offset = AST_FRIENDLY_OFFSET;
					myf.f.src = __PRETTY_FUNCTION__;
					myf.f.data = myf.frdata;
					if (ast_write(chan, &myf.f) < 0) {
						res = -1;
						break;
					}
					if (res < needed) { /* last frame */
						ast_log(LOG_DEBUG, "Last frame\n");
						res=0;
						break;
					}
				} else {
					ast_log(LOG_DEBUG, "No more waveform\n");
					res = 0;
				}
			}
			ast_frfree(f);
		}
	}
	close(fds[0]);
	close(fds[1]);

/*	if (pid > -1) */
/*		kill(pid, SIGKILL); */
	if (!res && owriteformat)
		ast_set_write_format(chan, owriteformat);
	return res;
}

#define MAXLEN 180
#define MAXFESTLEN 2048




static int festival_exec(struct ast_channel *chan, void *vdata)
{
	int usecache;
	int res=0;
	struct localuser *u;
 	struct sockaddr_in serv_addr;
	struct hostent *serverhost;
	struct ast_hostent ahp;
	int fd;
	FILE *fs;
	char *host;
	char *cachedir;
	char *temp;
	char *festivalcommand;
	int port=1314;
	int n;
	char ack[4];
	char *waveform;
	int filesize;
	int wave;
	char bigstring[MAXFESTLEN];
	int i;
	struct MD5Context md5ctx;
	unsigned char MD5Res[16];
	char MD5Hex[33] = "";
	char koko[4] = "";
	char cachefile[MAXFESTLEN]="";
	int readcache=0;
	int writecache=0;
	int strln;
	int fdesc = -1;
	char buffer[16384];
	int seekpos = 0;	
	char data[256] = "";
	char *intstr;
	
	struct ast_config *cfg;
	cfg = ast_config_load(FESTIVAL_CONFIG);
	if (!cfg) {
		ast_log(LOG_WARNING, "No such configuration file %s\n", FESTIVAL_CONFIG);
		return -1;
	}
	if (!(host = ast_variable_retrieve(cfg, "general", "host"))) {
		host = "localhost";
	}
	if (!(temp = ast_variable_retrieve(cfg, "general", "port"))) {
		port = 1314;
	} else {
		port = atoi(temp);
	}
	if (!(temp = ast_variable_retrieve(cfg, "general", "usecache"))) {
		usecache=0;
	} else {
		usecache = ast_true(temp);
	}
	if (!(cachedir = ast_variable_retrieve(cfg, "general", "cachedir"))) {
		cachedir = "/tmp/";
	}
	if (!(festivalcommand = ast_variable_retrieve(cfg, "general", "festivalcommand"))) {
		festivalcommand = "(tts_textasterisk \"%s\" 'file)(quit)\n";
	}
	if (!vdata || ast_strlen_zero(vdata)) {
		ast_log(LOG_WARNING, "festival requires an argument (text)\n");
		ast_config_destroy(cfg);
		return -1;
	}
	strncpy(data, vdata, sizeof(data) - 1);
	if ((intstr = strchr(data, '|'))) {
		*intstr = '\0';
		intstr++;
		if (!strcasecmp(intstr, "any"))
			intstr = AST_DIGIT_ANY;
	}
	LOCAL_USER_ADD(u);
	ast_log(LOG_DEBUG, "Text passed to festival server : %s\n",(char *)data);
	/* Connect to local festival server */
	
    	fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    	if (fd < 0) {
		ast_log(LOG_WARNING,"festival_client: can't get socket\n");
		ast_config_destroy(cfg);
        	return -1;
	}
        memset(&serv_addr, 0, sizeof(serv_addr));
        if ((serv_addr.sin_addr.s_addr = inet_addr(host)) == -1) {
	        /* its a name rather than an ipnum */
	        serverhost = ast_gethostbyname(host, &ahp);
	        if (serverhost == (struct hostent *)0) {
        	    	ast_log(LOG_WARNING,"festival_client: gethostbyname failed\n");
			ast_config_destroy(cfg);
	            	return -1;
        	}
	        memmove(&serv_addr.sin_addr,serverhost->h_addr, serverhost->h_length);
    	}
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);

	if (connect(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
		ast_log(LOG_WARNING,"festival_client: connect to server failed\n");
		ast_config_destroy(cfg);
        	return -1;
    	}
    	
    	/* Compute MD5 sum of string */
    	MD5Init(&md5ctx);
    	MD5Update(&md5ctx,(unsigned char const *)data,strlen(data));
    	MD5Final(MD5Res,&md5ctx);
		MD5Hex[0] = '\0';
    	
    	/* Convert to HEX and look if there is any matching file in the cache 
    		directory */
    	for (i=0;i<16;i++) {
    		snprintf(koko, sizeof(koko), "%X",MD5Res[i]);
    		strncat(MD5Hex, koko, sizeof(MD5Hex) - strlen(MD5Hex) - 1);
    	}
    	readcache=0;
    	writecache=0;
    	if (strlen(cachedir)+strlen(MD5Hex)+1<=MAXFESTLEN && (usecache==-1)) {
    		snprintf(cachefile, sizeof(cachefile), "%s/%s", cachedir, MD5Hex);
    		fdesc=open(cachefile,O_RDWR);
    		if (fdesc==-1) {
    			fdesc=open(cachefile,O_CREAT|O_RDWR,0777);
    			if (fdesc!=-1) {
    				writecache=1;
    				strln=strlen((char *)data);
    				ast_log(LOG_DEBUG,"line length : %d\n",strln);
    				write(fdesc,&strln,sizeof(int));
    				write(fdesc,data,strln);
				seekpos=lseek(fdesc,0,SEEK_CUR);
				ast_log(LOG_DEBUG,"Seek position : %d\n",seekpos);
    			}
    		} else {
    			read(fdesc,&strln,sizeof(int));
    			ast_log(LOG_DEBUG,"Cache file exists, strln=%d, strlen=%d\n",strln,(int)strlen((char *)data));
    			if (strlen((char *)data)==strln) {
    				ast_log(LOG_DEBUG,"Size OK\n");
    				read(fdesc,&bigstring,strln);
	    			bigstring[strln] = 0;
				if (strcmp(bigstring,data)==0) { 
	    				readcache=1;
	    			} else {
	    				ast_log(LOG_WARNING,"Strings do not match\n");
	    			}
	    		} else {
	    			ast_log(LOG_WARNING,"Size mismatch\n");
	    		}
	    	}
	}
    			
	if (readcache==1) {
		close(fd);
		fd=fdesc;
		ast_log(LOG_DEBUG,"Reading from cache...\n");
	} else {
		ast_log(LOG_DEBUG,"Passing text to festival...\n");
    		fs=fdopen(dup(fd),"wb");
		fprintf(fs,festivalcommand,(char *)data);
		fflush(fs);
		fclose(fs);
	}
	
	/* Write to cache and then pass it down */
	if (writecache==1) {
		ast_log(LOG_DEBUG,"Writing result to cache...\n");
		while ((strln=read(fd,buffer,16384))!=0) {
			write(fdesc,buffer,strln);
		}
		close(fd);
		close(fdesc);
		fd=open(cachefile,O_RDWR);
		lseek(fd,seekpos,SEEK_SET);
	}
	
	ast_log(LOG_DEBUG,"Passing data to channel...\n");
	
	/* Read back info from server */
	/* This assumes only one waveform will come back, also LP is unlikely */
	wave = 0;
	do {
		for (n=0; n < 3; )
			n += read(fd,ack+n,3-n);
		ack[3] = '\0';
		if (strcmp(ack,"WV\n") == 0) {         /* receive a waveform */
			ast_log(LOG_DEBUG,"Festival WV command\n");
			waveform = socket_receive_file_to_buff(fd,&filesize);
			res = send_waveform_to_channel(chan,waveform,filesize, intstr);
			free(waveform);
			break;
		}
		else if (strcmp(ack,"LP\n") == 0) {   /* receive an s-expr */
			ast_log(LOG_DEBUG,"Festival LP command\n");
			waveform = socket_receive_file_to_buff(fd,&filesize);
			waveform[filesize]='\0';
			ast_log(LOG_WARNING,"Festival returned LP : %s\n",waveform);
			free(waveform);
		} else if (strcmp(ack,"ER\n") == 0) {    /* server got an error */
			ast_log(LOG_WARNING,"Festival returned ER\n");
			res=-1;
			break;
    		}
	} while (strcmp(ack,"OK\n") != 0);
	close(fd);
	ast_config_destroy(cfg);
	LOCAL_USER_REMOVE(u);                                                                                
	return res;

}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	
	return ast_register_application(app, festival_exec, synopsis, descrip);
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
