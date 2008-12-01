/*
 * Asterisk EAGI -> TCP/IP proxy
 * 	by Danijel Korzinek (devil_slayer _at_ hotmail.com)
 *
 * This simple C application allows you to control asterisk thru one TCP/IP
 * socket and listen to the conversation thru another socket.
 *
 * Great for ASR or wizzard-of-oz telephony systems!
 *
 * HOWTO:
 * 	The program is compiled using the following command:
 * 		gcc eagi_proxy.c -o eagi_proxy -lpthread
 *
 * 	In the dialplan, you can add something like this to the main context:
 * 		exten => s,1,Answer
 * 		exten => s,n,EAGI(/path/to/eagi_proxy)
 * 		exten => s,n,Hangup
 *
 * 	To test the program you can use the netcat utility:
 * 		(http://netcat.sourceforge.net/)
 *
 * 		-in one console run:
 * 			nc -vv -l -p 8418 > /path/to/file.raw
 * 		-in another run:
 * 			nc -vv -l -p 8417
 * 		-you can use any file for the signal or even /dev/null
 * 		-you can change the port numbers in the sourcecode below
 *
 * 	Once you make the call, both programs will accept the incoming
 * 	connection. The program on port 8417 will print out the enviornemnt
 * 	(unless the appropriate define below is commented) and you can write
 * 	any AGI command there (http://www.voip-info.org/wiki-Asterisk+AGI),
 * 	e.g.:
 * 		GET DATA /path/to/gsm/file 10000 4
 *
 * 	Finally, you can open the RAW file in any sound editor. The format is:
 * 		RAW little-endian 8kHz 16bit
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <pthread.h>

/* DEFINES */
#define SIGNAL_PORT 8418
#define COMMAND_PORT 8417
#define SEND_ENVIORNMENT /*send the enviornment thru the socket*/
/************************/


#define BUFSIZE 1024
char buf[BUFSIZE];

#define WINSIZE 400 /* 25 ms @ 8 kHz and 16bit */
char window[WINSIZE];

#define WINBUF_NUM 2400 /* number of WINSIZE windows = 1 minute */
char* winbuf;
char *end, *bs, *be;
/* winbuf - start of buffer 
 * end - end of buffer  
 * bs - start of data
 * be - end of data 
 */

int command_desc; /* command transfer descriptor */
int speech_desc; /* speech signal descrriptor */
char connected=1; /* connection state */

int connect_to_host(char* host, int port); /* connect to a given host (name or IP) and given port number in nonblocking mode returning socket descriptor*/

void read_full(int file, char* buffer, int num); /* read EXACTLY "num" ammount of bytes from "file" descriptor to "buffer"*/
int read_some(int file, char* buffer, int size); /* read AT MOST "size" ammount of bytes */

void write_buf(int file, char* buffer, int num); /* write "num" ammount of bytes to "file" descriptor and buffer the surplus if the write would block */
int write_amap(int file, char* buffer, int num); /*write AT MOST "num" ammount of bytes and return ammount that was written*/

void setnonblocking(int desc); /*sets the socket non-blocking; for polling */

void finalize(); /* this function is run at exit */

pthread_mutex_t command_mutex;/* command socket mutex */
pthread_t stdin_thread,signal_thread;
void* readStdin(void* ptr);
void* readSignal(void* ptr);

/* The program creates 3 threads:
 * 1) Main thread - reads commands from the socket and sends them to asterisk
 * 2) stdin_thread - reads asterisk output and sends it to the command socket
 * 3) signal_thread - reads the sound from asterisk and sends it to the signal socket
 */

int main()
{
	int ret;
	
	atexit(finalize);

	setlinebuf(stdin);
	setlinebuf(stdout);

	winbuf=(char*)malloc(WINSIZE*WINBUF_NUM);
	end=winbuf+WINSIZE*WINBUF_NUM;
	bs=be=winbuf;

	speech_desc=connect_to_host("localhost",SIGNAL_PORT);
	if(speech_desc<0) 
	{
		perror("signal socket");
		return -1;
	}


	command_desc=connect_to_host("localhost",COMMAND_PORT);
	if(command_desc<0) 
	{
		perror("command socket");
		return -1;
	}

	pthread_mutex_init(&command_mutex,NULL);
	pthread_create(&stdin_thread,NULL,readStdin,NULL);
	pthread_create(&signal_thread,NULL,readSignal,NULL);

	while(connected)
	{	
		pthread_mutex_lock(&command_mutex);
		ret=read_some(command_desc,buf,BUFSIZE);
		pthread_mutex_unlock(&command_mutex);
		if(ret>0)
		{
			buf[ret]=0;
			printf("%s",buf);			
		}
	}

	return 0;
}

void finalize()
{
	close(command_desc);
	close(speech_desc);
	free(winbuf);	
}

void* readStdin(void* ptr)
{
	while(1)/*read enviornment*/
	{
		fgets(buf,BUFSIZE,stdin);
		#ifdef SEND_ENVIORNMENT
			pthread_mutex_lock(&command_mutex);
			write_buf(command_desc,buf,strlen(buf));
			pthread_mutex_unlock(&command_mutex);
		#endif
		if(feof(stdin) || buf[0]=='\n') 
		{
			break;
		}
	}

	while(connected)
	{
		fgets(buf,BUFSIZE,stdin);
		pthread_mutex_lock(&command_mutex);
		write_buf(command_desc,buf,strlen(buf));
		pthread_mutex_unlock(&command_mutex);
	}

	pthread_exit(NULL);
}

void* readSignal(void* ptr)
{
	while(connected)
	{
		read_full(3,window,WINSIZE);
		write_buf(speech_desc,window,WINSIZE);
	}

	pthread_exit(NULL);
}


void read_full(int file, char* buffer, int num)
{
	int count,pos=0;

	while(num)
	{
		count=read(file,buffer+pos,num);
		if(count==0 || (count<0 && errno!=EAGAIN))
		{
			connected=0;
			return;
		}
		num-=count;
		pos+=count;
	}
}

int connect_to_host(char* name, int port)
{
	int address;
	struct hostent* host_entity;
	int res,desc;
	int opts;
	struct sockaddr_in host;
	

	/* get adress */	
	if(!strcmp(name,"localhost"))
		address=htonl(2130706433); /*127.0.0.1*/
	else
	{
		address=inet_addr(name); /* check if it's an IP that's written in the string */
		if(address==(in_addr_t)-1)
		{
			host_entity = gethostbyname(name); /* search for the host under this name */
	
			if(!host_entity)
			{
				fprintf(stderr,"EAGI proxy: Wrong address!\n"); /* can't find anything*/
				return -1;
			}
			address=*((int*)host_entity->h_addr);
		}
	}

	desc=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
	if(desc<0)
	{
		fprintf(stderr,"EAGI proxy: Cannot create socket!\n");
		return -1;
	}

	memset((void*)&host,0,sizeof(struct sockaddr_in));
	
	host.sin_family=AF_INET;
	host.sin_port=htons(port);
	host.sin_addr.s_addr=address;
	
	res=connect(desc,(struct sockaddr*)&host,sizeof(host));
	if(res<0)
	{
		fprintf(stderr,"EAGI proxy: Cannot connect!\n");
		return -1;
	}

	/* set to non-blocking mode */
	opts = fcntl(desc,F_GETFL);
	if (opts < 0) {
		perror("fcntl(F_GETFL)");
		exit(EXIT_FAILURE);
	}
	opts = (opts | O_NONBLOCK);
	if (fcntl(desc,F_SETFL,opts) < 0) {
		perror("fcntl(F_SETFL)");
		exit(EXIT_FAILURE);
	}


	return desc;
}

int read_some(int desc, char* buffer, int size)
{
	unsigned char c;
	int res,i=0;

	for(;;)
	{
		res=read(desc,&c,1);
		if(res<1)
		{
			if(errno!=EAGAIN)
			{
				perror("Error reading");
				connected=0;
			}	
			break;
		}
		if(res==0) 
		{
			connected=0;
			break;
		}
		
		buffer[i]=c;
		i++;
	}

	return i;
}

/* This is a tricky function! */
void write_buf(int desc, char* buf, int size)
{
	int ret;

	/*NOTE: AMAP -> as much as possible */

	if(be!=bs)/* if data left in buffer */
	{
		if(be>bs)/* if buffer not split */
		{
			ret=write_amap(desc,bs,be-bs);/* write AMAP */
			bs+=ret;/* shift the start of the buffer */
		}
		else/* if buffer is split */
		{
			ret=write_amap(desc,bs,end-bs);/* write higher part first */
			if(ret==end-bs)/* if wrote whole of the higher part */
			{
				ret=write_amap(desc,winbuf,be-winbuf);/* write lower part */
				bs=winbuf+ret;/* shift start to new position */
			}
			else bs+=ret;/* if not wrote whole of higher part, only shift start */
		}
	}

	if(be==bs)/* if buffer is empty now */
	{
		ret=write_amap(desc,buf,size);/* write AMAP of the new data */
		buf+=ret;/* shift start of new data */
		size-=ret;/* lower size of new data */
	}

	if(size)/* if new data still remains unsent */
	{
		if(be>=bs)/* if data not split */
		{
			if(size>end-be)/* if new data size doesn't fit higher end */
			{
				size-=end-be;/* reduce new data size by the higher end size */
				memcpy(be,buf,end-be);/* copy to higher end */
				be=winbuf;/* shift end to begining of buffer */
				buf+=end-be;/* shift start of new data */
			}
			else/* if new data fits the higher end */
			{
				memcpy(be,buf,size);/* copy to higher end */
				be+=size;/* shift end by size */
				if(be>=end)/* if end goes beyond the buffer */
					be=winbuf;/* restart */
				size=0;/* everything copied */
			}
		}

		if(size)/* if new data still remains */
		{
			if(size>=bs-be)/* if new data doesn't fit between end and start */
			{
				fprintf(stderr,"Buffer overflow!\n");
				size=bs-be-1;/* reduce the size that we can copy */
			}

			if(size)/* if we can copy anything */
			{
				memcpy(be,buf,size);/* copy the new data between end and start */
				be+=size;/* shift end by size */
			}
		}
	}
}

int write_amap(int desc, char* buf, int size)
{
	int ret;
	ret=write(desc,buf,size);
	if(ret<0)
	{
		if(errno!=EAGAIN) 
		{
			perror("Error writing");
			connected=0;
		}
		return 0;
	}
	if(ret==0)
		connected=0;

	return ret;
}


void setnonblocking(int desc)
{
	int opts;
	
	opts = fcntl(desc,F_GETFL);
	if(opts < 0)
	{
		perror("fcntl(F_GETFL)");
		exit(-1);
	}

	opts = (opts | O_NONBLOCK );
	if(fcntl(desc,F_SETFL,opts) < 0)
	{
		perror("fcntl(F_SETFL)");
		exit(-1);
	}
}
