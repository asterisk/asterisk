/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * SMS application - ETSI ES 201 912 protocol 1 implimentation
 * 
 * Copyright (C) 2004, Adrian Kennard, rights assigned to Digium
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/callerid.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>

#include <pthread.h>

/* ToDo */
/* When acting as SC and answering, should check for messages and send instead of sending EST as first packet */
/* Add full VP support */
/* Handle status report messages (generation and reception) */
/* Log to show oa and da with no spaces to allow parsing */
/* USC2 coding */

static unsigned char message_ref;	/* arbitary message ref */

static char *tdesc = "SMS/PSTN handler";

static char *app = "SMS";

static char *synopsis = "Communicates with SMS service centres and SMS capable analogue phones";

static char *descrip =
  "  SMS(name|[a][s]):  SMS handles exchange of SMS data with a call to/from SMS capabale\n"
  "phone or SMS PSTN service centre. Can send and/or receive SMS messages.\n"
  "Returns 0 if call handled correctly, or -1 if there were any problems.\n"
  "Works to ETSI ES 201 912 compatible with BT SMS PSTN service in UK\n"
  "Typical usage is to use to handle called from the SMS service centre CLI,\n"
  "or to set up a call using 'outgoing' or manager interface to connect service centre to SMS()\n"
  "name is the name of the queue used in /var/spool/asterisk/sms\n"
  "Argument 'a' means answer, i.e. send initial FSK packet.\n"
  "Argument 's' means act as service centre talking to a phone.\n"
  "Messages are processed as per text file message queues.\n"
  "Can also call as SMS(name|[s]|number|message) to queue a message.\n";

static signed short wave[] =
  { 0, 392, 782, 1167, 1545, 1913, 2270, 2612, 2939, 3247, 3536, 3802, 4045, 4263, 4455, 4619, 4755, 4862, 4938, 4985,
  5000, 4985, 4938, 4862, 4755, 4619, 4455, 4263, 4045, 3802, 3536, 3247, 2939, 2612, 2270, 1913, 1545, 1167, 782, 392,
  0, -392, -782, -1167,
  -1545, -1913, -2270, -2612, -2939, -3247, -3536, -3802, -4045, -4263, -4455, -4619, -4755, -4862, -4938, -4985, -5000,
  -4985, -4938, -4862,
  -4755, -4619, -4455, -4263, -4045, -3802, -3536, -3247, -2939, -2612, -2270, -1913, -1545, -1167, -782, -392
};

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

/* SMS 7 bit character mapping */
/* Note that some greek characters are simply coded as 191 (inverted question mark) as ISO-8859-1 does not do greek */
/* Note 27 (escape) is to be displayed as a space as per GSM 03.38 */
static unsigned char sms7to8[] = {
  '@', 163, '$', 165, 232, 233, 249, 236, 242, 199, 10, 216, 248, 13, 197, 229,
  191, '_', 191, 191, 191, 191, 191, 191, 191, 191, 191, ' ', 198, 230, 223, 201,
  ' ', '!', '"', '#', 164, '%', '&', 39, '(', ')', '*', '+', ',', '-', '.', '/',
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':', ';', '<', '=', '>', '?',
  161, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
  'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 196, 214, 209, 220, 167,
  191, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',
  'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 228, 246, 241, 252, 224,
};
unsigned char sms8to7[256];

typedef struct sms_s
{
  unsigned char hangup;		/* we are done... */
  unsigned char smsc;		/* we are SMSC */
  char queue[30];		/* queue name */
  char oa[20];			/* originating address */
  char da[20];			/* destination address */
  time_t scts;			/* time stamp */
  unsigned char pid;		/* protocol ID */
  unsigned char dcs;		/* data coding scheme */
  unsigned char mr;		/* message reference */
  unsigned char udl;		/* user date length */
  unsigned char srr:1;		/* Status Report request */
  unsigned char rp:1;		/* Reply Path */
  unsigned int vp;		/* validity period in minutes, 0 for not set */
  unsigned char ud[160];	/* user data (message) */
  unsigned char cli[20];	/* caller ID */
  unsigned char ophase;		/* phase (0-79) for 0 and 1 frequencies (1300Hz and 2100Hz) */
  unsigned char ophasep;	/* phase (0-79) for 1200 bps */
  unsigned char obyte;		/* byte being sent */
  unsigned int opause;		/* silent pause before sending (in sample periods) */
  unsigned char obitp;		/* bit in byte */
  unsigned char osync;		/* sync bits to send */
  unsigned char obytep;		/* byte in data */
  unsigned char obyten;		/* bytes in data */
  unsigned char omsg[256];	/* data buffer (out) */
  unsigned char imsg[200];	/* data buffer (in) */
  signed long long ims0, imc0, ims1, imc1;	/* magnitude averages sin/cos 0/1 */
  unsigned int idle;
  unsigned short imag;		/* signal level */
  unsigned char ips0, ips1, ipc0, ipc1;	/* phase sin/cos 0/1 */
  unsigned char ibitl;		/* last bit */
  unsigned char ibitc;		/* bit run length count */
  unsigned char iphasep;	/* bit phase (0-79) for 1200 bps */
  unsigned char ibitn;		/* bit number in byte being received */
  unsigned char ibytev;		/* byte value being received */
  unsigned char ibytep;		/* byte pointer in messafe */
  unsigned char ibytec;		/* byte checksum for message */
  unsigned char ierr;		/* error flag */
  unsigned char ibith;		/* history of last bits */
  unsigned char ibitt;		/* total of 1's in last 3 bites */
  /* more to go here */
} sms_t;

static void *
sms_alloc (struct ast_channel *chan, void *params)
{
  return params;
}

static void
sms_release (struct ast_channel *chan, void *data)
{
  return;
}

static void sms_messagetx (sms_t * h);

/* copy number, skipping non digits apart from leading + */
static void
numcpy (char *d, char *s)
{
  if (*s == '+')
    *d++ = *s++;
  while (*s)
    {
      if (isdigit (*s))
	*d++ = *s;
      s++;
    }
  *d = 0;
}

static char *
isodate (time_t t)
{				/* static, return a date/time in ISO format */
  static char date[20];
  strftime (date, sizeof (date), "%Y-%m-%d %H:%M:%S", localtime (&t));
  return date;
}

/* pack n bytes from i to o and return number of bytes */
static unsigned char
pack7 (unsigned char *o, unsigned char *i, unsigned char n)
{
  unsigned char p = 0, b = 0;
  /* fixup - map character set perhaps... */
  o[0] = 0;
  while (n--)
    {
      o[p] |= ((sms8to7[*i] & 0x7F) << b);
      b += 7;
      if (b >= 8)
	{
	  b -= 8;
	  p++;
	  o[p] = ((sms8to7[*i] & 0x7F) >> (7 - b));
	}
      i++;
    }
  if (b)
    p++;
  return p;
}

/* check if all characters are valid 7 bit coding characters */
static unsigned char
check7 (unsigned char l, unsigned char *p)
{
  while (l--)
    if (sms8to7[*p++] & 0x80)
      return 1;
  return 0;
}

/* pack a date and return */
static void
packdate (unsigned char *o, time_t w)
{
  struct tm *t = localtime (&w);
  int z = timezone / 3600 / 15;
  *o++ = ((t->tm_year % 10) << 4) + (t->tm_year % 100) / 10;
  *o++ = (((t->tm_mon + 1) % 10) << 4) + (t->tm_mon + 1) / 10;
  *o++ = ((t->tm_mday % 10) << 4) + t->tm_mday / 10;
  *o++ = ((t->tm_hour % 10) << 4) + t->tm_hour / 10;
  *o++ = ((t->tm_min % 10) << 4) + t->tm_min / 10;
  *o++ = ((t->tm_sec % 10) << 4) + t->tm_sec / 10;
  if (z < 0)
    *o++ = (((-z) % 10) << 4) + (-z) / 10 + 0x08;
  else
    *o++ = ((z % 10) << 4) + z / 10;
}

/* unpack a date and return */
static time_t
unpackdate (unsigned char *i)
{
  struct tm t;
  t.tm_year = 100 + (i[0] & 0xF) * 10 + (i[0] >> 4);
  t.tm_mon = (i[1] & 0xF) * 10 + (i[1] >> 4) - 1;
  t.tm_mday = (i[2] & 0xF) * 10 + (i[2] >> 4);
  t.tm_hour = (i[3] & 0xF) * 10 + (i[3] >> 4);
  t.tm_min = (i[4] & 0xF) * 10 + (i[4] >> 4);
  t.tm_sec = (i[5] & 0xF) * 10 + (i[5] >> 4);
  t.tm_isdst = 0;
  if (i[6] & 0x08)
    t.tm_min += 15 * ((i[6] & 0x7) * 10 + (i[6] >> 4));
  else
    t.tm_min -= 15 * ((i[6] & 0x7) * 10 + (i[6] >> 4));
  return mktime (&t);
}

/* unpack bytes from i to o and return number of source bytes. */
static unsigned char
unpack7 (unsigned char *o, unsigned char *i, unsigned char l)
{
  unsigned char b = 0, p = 0;
  while (l--)
    {
      if (b < 2)
	*o++ = sms7to8[((i[p] >> b) & 0x7F)];
      else
	*o++ = sms7to8[((((i[p] >> b) + (i[p + 1] << (8 - b)))) & 0x7F)];
      b += 7;
      if (b >= 8)
	{
	  b -= 8;
	  p++;
	}
    }
  if (b)
    p++;
  return p;
}

/* unpack an address from i, return byte length, unpack to o */
static unsigned char
unpackaddress (char *o, unsigned char *i)
{
  unsigned char l = i[0], p;
  if (i[1] == 0x91)
    *o++ = '+';
  for (p = 0; p < l; p++)
    {
      if (p & 1)
	*o++ = (i[2 + p / 2] >> 4) + '0';
      else
	*o++ = (i[2 + p / 2] & 0xF) + '0';
    }
  *o = 0;
  return (l + 5) / 2;
}

/* store an address at o, and return number of bytes used */
static unsigned char
packaddress (unsigned char *o, char *i)
{
  unsigned char p = 2;
  o[0] = 0;
  if (*i == '+')
    {
      i++;
      o[1] = 0x91;
    }
  else
    o[1] = 0x81;
  while (*i)
    if (isdigit (*i))
      {
	if (o[0] & 1)
	  o[p++] |= ((*i & 0xF) << 4);
	else
	  o[p] = (*i & 0xF);
	o[0]++;
	i++;
      }
    else
      i++;
  if (o[0] & 1)
    o[p++] |= 0xF0;		/* pad */
  return p;
}

static void
sms_log (sms_t * h, char status)
{				/* log the output, and remove file */
  if (*h->oa || *h->da)
    {
      int o = open ("/var/log/asterisk/sms", O_CREAT | O_APPEND | O_WRONLY, 0666);
      if (o >= 0)
	{
	  char line[1000], *p;
	  unsigned char n;
	  sprintf (line, "%s %c %s %s %s ", isodate (time (0)), status, h->queue, *h->oa ? h->oa : "-",
		   *h->da ? h->da : "-");
	  p = line + strlen (line);
	  for (n = 0; n < h->udl; n++)
	    if (h->ud[n] == '\\')
	      {
		*p++ = '\\';
		*p++ = '\\';
	      }
	    else if (h->ud[n] == '\n')
	      {
		*p++ = '\\';
		*p++ = 'n';
	      }
	    else if (h->ud[n] == '\r')
	      {
		*p++ = '\\';
		*p++ = 'r';
	      }
	    else if (h->ud[n] < 32 || h->ud[n] == 127)
	      *p++ = 191;
	    else
	      *p++ = h->ud[n];
	  *p++ = '\n';
	  *p = 0;
	  write (o, line, strlen (line));
	  close (o);
	}
      *h->oa = *h->da = h->udl = 0;
    }
}

/* parse and delete a file */
static void
sms_readfile (sms_t * h, char *fn)
{
  char line[1000];
  FILE *s;
  char dcsset = 0;		/* if DSC set */
  ast_log (LOG_EVENT, "Sending %s\n", fn);
  h->udl = *h->oa = *h->da = h->pid = h->srr = h->rp = h->vp = 0;
  h->dcs = 0xF1;		/* normal messages class 1 */
  h->scts = time (0);
  h->mr = message_ref++;
  s = fopen (fn, "r");
  if (s)
    {
      if (unlink (fn))
	{			/* concurrent access, we lost */
	  fclose (s);
	  return;
	}
      while (fgets (line, sizeof (line), s))
	{			/* process line in file */
	  char *p;
	  for (p = line; *p && *p != '\n' && *p != '\r'; p++);
	  *p = 0;		/* strip eoln */
	  //ast_log (LOG_EVENT, "Line %s\n", line);
	  p = line;
	  if (!*p || *p == ';')
	    continue;		/* blank line or comment, ignore */
	  while (isalnum (*p))
	    {
	      *p = tolower (*p);
	      p++;
	    }
	  while (isspace (*p))
	    *p++ = 0;
	  if (*p == '=')
	    {
	      *p++ = 0;
	      if (!strcmp (line, "ud"))
		{		/* parse message */
		  unsigned char o = 0;
		  while (*p && o < 160)
		    {
		      if (*p == '\\')
			{
			  p++;
			  if (*p == '\\')
			    h->ud[o++] = *p++;
			  else if (*p == 'n')
			    {
			      h->ud[o++] = '\n';
			      p++;
			    }
			  else if (*p == 'r')
			    {
			      h->ud[o++] = '\r';
			      p++;
			    }
			}
		      else
			h->ud[o++] = *p++;
		    }
		  h->udl = o;
		  if (*p)
		    ast_log (LOG_WARNING, "UD too long in %s\n", fn);
		}
	      else
		{
		  while (isspace (*p))
		    p++;
		  if (!strcmp (line, "oa") && strlen (p) < sizeof (h->oa))
		    numcpy (h->oa, p);
		  else if (!strcmp (line, "da") && strlen (p) < sizeof (h->oa))
		    numcpy (h->da, p);
		  else if (!strcmp (line, "pid"))
		    h->pid = atoi (p);
		  else if (!strcmp (line, "dcs"))
		    {
		      h->dcs = atoi (p);
		      dcsset = 1;
		    }
		  else if (!strcmp (line, "mr"))
		    h->mr = atoi (p);
		  else if (!strcmp (line, "srr"))
		    h->srr = (atoi (p) ? 1 : 0);
		  else if (!strcmp (line, "vp"))
		    h->vp = atoi (p);
		  else if (!strcmp (line, "rp"))
		    h->rp = (atoi (p) ? 1 : 0);
		  else if (!strcmp (line, "scts"))
		    {		/* get date/time */
		      int Y, m, d, H, M, S;
		      if (sscanf (p, "%d-%d-%d %d:%d:%d", &Y, &m, &d, &H, &M, &S) == 6)
			{
			  struct tm t;
			  t.tm_year = Y - 1900;
			  t.tm_mon = m - 1;
			  t.tm_mday = d;
			  t.tm_hour = H;
			  t.tm_min = M;
			  t.tm_sec = S;
			  t.tm_isdst = -1;
			  h->scts = mktime (&t);
			  if (h->scts == (time_t) - 1)
			    ast_log (LOG_WARNING, "Bad date/timein %s: %s", fn, p);
			}
		    }
		  else
		    ast_log (LOG_WARNING, "Cannot parse in %s: %s=%si\n", fn, line, p);
		}
	    }
	  else if (*p == '#')
	    {			/* raw hex format */
	      *p++ = 0;
	      if (!strcmp (line, "ud"))
		{
		  unsigned char o = 0;
		  while (*p && o < 160)
		    {
		      if (isxdigit (*p) && isxdigit (p[1]))
			{
			  h->ud[o] =
			    (((isalpha (*p) ? 9 : 0) + (*p & 0xF)) << 4) + ((isalpha (p[1]) ? 9 : 0) + (p[1] & 0xF));
			  o++;
			  p += 2;
			}
		      else
			break;
		    }
		  h->udl = o;
		  if (*p)
		    ast_log (LOG_WARNING, "UD too long / invalid hex in %s\n", fn);
		}
	      else
		ast_log (LOG_WARNING, "Only ud can use 8 bit key format with # instead of =\n");
	    }
	  else
	    ast_log (LOG_WARNING, "Cannot parse in %s: %s\n", fn, line);
	}
      fclose (s);
      if (!dcsset && h->udl <= 140 && check7 (h->udl, h->ud))
	{
	  h->dcs = 0xF5;	// default to 8 bit
	  ast_log (LOG_WARNING, "Sending in 8 bit format because of illegal characters %s\n", fn);
	}
      if ((h->dcs & 4) && h->udl > 140)
	{
	  ast_log (LOG_WARNING, "8 bit data too long, truncated %s\n", fn);
	  h->udl = 140;
	}
      else if (!(h->dcs & 4) && check7 (h->udl, h->ud))
	ast_log (LOG_WARNING, "Invalid 7 bit GSM data %s\n", fn);
    }
  //ast_log (LOG_EVENT, "Loaded %s\n", fn);
}

/* white a received text message to a file */
static void
sms_writefile (sms_t * h)
{
  char fn[200], fn2[200];
  FILE *o;
  strcpy (fn, "/var/spool/asterisk/sms");
  mkdir (fn, 0777);		/* ensure it exists */
  sprintf (fn + strlen (fn), "/%s.%s", h->smsc ? "me-sc" : "sc-me", h->queue);
  mkdir (fn, 0777);		/* ensure it exists */
  strcpy (fn2, fn);
  strftime (fn2 + strlen (fn2), 30, "/%Y-%m-%d_%H:%M:%S", localtime (&h->scts));
  sprintf (fn2 + strlen (fn2), "-%02X", h->mr);
  sprintf (fn + strlen (fn), "/.%s", fn2 + strlen (fn) + 1);
  o = fopen (fn, "w");
  if (o)
    {
      fprintf (o, "mr=%d\n", h->mr);
      if (*h->oa)
	fprintf (o, "oa=%s\n", h->oa);
      if (*h->da)
	fprintf (o, "da=%s\n", h->da);
      if (h->pid)
	fprintf (o, "pid=%d\n", h->pid);
      if (h->dcs != 0xF1)
	fprintf (o, "dcs=%d\n", h->dcs);
      if (h->vp)
	fprintf (o, "srr=%d\n", h->vp);
      if (h->srr)
	fprintf (o, "srr=1\n");
      if (h->rp)
	fprintf (o, "rp=1\n");
      if (h->scts)
	fprintf (o, "scts=%s\n", isodate (h->scts));
      if (h->udl)
	{
	  unsigned int p;
	  for (p = 0; p < h->udl && ((h->ud[p] >= 32 && h->ud[p] != 127) || h->ud[p] == '\n' || h->ud[p] == '\r'); p++);
	  if (p < h->udl)
	    {			// use a hex format as unprintable characters
	      fprintf (o, "ud#");
	      for (p = 0; p < h->udl; p++)
		fprintf (o, "%02X", h->ud[p]);
	      fprintf (o, "\n;");
	      /* followed by commented line using printable characters */
	    }
	  fprintf (o, "ud=");
	  for (p = 0; p < h->udl; p++)
	    {
	      if (h->ud[p] == '\\')
		fprintf (o, "\\\\");
	      else if (h->ud[p] == '\r')
		fprintf (o, "\\r");
	      else if (h->ud[p] == '\n')
		fprintf (o, "\\n");
	      else if (h->ud[p] < 32 || h->ud[p] == 127)
		fputc (191, o);
	      else
		fputc (h->ud[p], o);
	    }
	  fprintf (o, "\n");
	}
      fclose (o);
      if (rename (fn, fn2))
	unlink (fn);
      else
	ast_log (LOG_EVENT, "Received to %s\n", fn2);
    }
}

/* read dir skipping dot files... */
static struct dirent *
readdirdot (DIR * d)
{
  struct dirent *f;
  do
    {
      f = readdir (d);
    }
  while (f && *f->d_name == '.');
  return f;
}

/* handle the incoming message */
static unsigned char
sms_handleincoming (sms_t * h)
{
  unsigned char p = 3;
  if (h->smsc)
    {				/* SMSC */
      if ((h->imsg[2] & 3) == 1)
	{			/* SMS-SUBMIT */
	  h->vp = 0;
	  h->srr = ((h->imsg[2] & 0x20) ? 1 : 0);
	  h->rp = ((h->imsg[2] & 0x80) ? 1 : 0);
	  strcpy (h->oa, h->cli);
	  h->scts = time (0);
	  h->mr = h->imsg[p++];
	  p += unpackaddress (h->da, h->imsg + p);
	  h->pid = h->imsg[p++];
	  h->dcs = h->imsg[p++];
	  if ((h->imsg[2] & 0x18) == 0x10)
	    {			/* relative VP */
	      if (h->imsg[p] < 144)
		h->vp = (h->imsg[p] + 1) * 5;
	      else if (h->imsg[p] < 168)
		h->vp = 720 + (h->imsg[p] - 143) * 30;
	      else if (h->imsg[p] < 197)
		h->vp = (h->imsg[p] - 166) * 1440;
	      else
		h->vp = (h->imsg[p] - 192) * 10080;
	      p++;
	    }
	  else if (h->imsg[2] & 0x18)
	    p += 7;		/* ignore enhanced / absolute VP */
	  h->udl = h->imsg[p++];
	  if (h->udl)
	    {
	      if (h->dcs & 4)
		{
		  memcpy (h->ud, h->imsg + p, h->udl);
		  p += h->udl;
		}
	      else
		p += unpack7 (h->ud, h->imsg + p, h->udl);
	    }
	  sms_writefile (h);	/* write the file */
	  if (p != h->imsg[1] + 2)
	    return 0xFF;	/* duh! */
	}
      else
	{
	  ast_log (LOG_WARNING, "Unknown message type %02X\n", h->imsg[2]);
	  return 0xFF;
	}
    }
  else
    {				/* client */
      if (!(h->imsg[2] & 3))
	{			/* SMS-DELIVER */
	  *h->da = h->srr = h->rp = h->vp = 0;
	  h->mr = message_ref++;
	  p += unpackaddress (h->oa, h->imsg + p);
	  h->pid = h->imsg[p++];
	  h->dcs = h->imsg[p++];
	  h->scts = unpackdate (h->imsg + p);
	  p += 7;
	  h->udl = h->imsg[p++];
	  if (h->udl)
	    {
	      if (h->dcs & 4)
		{
		  memcpy (h->ud, h->imsg + p, h->udl);
		  p += h->udl;
		}
	      else
		p += unpack7 (h->ud, h->imsg + p, h->udl);
	    }
	  sms_writefile (h);	/* write the file */
	  if (p != h->imsg[1] + 2)
	    return 0xFF;	/* duh! */
	}
      else
	{
	  ast_log (LOG_WARNING, "Unknown message type %02X\n", h->imsg[2]);
	  return 0xFF;
	}
    }
  return 0;			/* no error */
}

static void
sms_nextoutgoing (sms_t * h)
{				/* find and fill in next message, or send a REL if none waiting */
  char fn[100 + NAME_MAX];
  DIR *d;
  char more = 0;
  strcpy (fn, "/var/spool/asterisk/sms");
  mkdir (fn, 0777);		/* ensure it exists */
  sprintf (fn + strlen (fn), "/%s.%s", h->smsc ? "sc-me" : "me-sc", h->queue);
  mkdir (fn, 0777);		/* ensure it exists */
  d = opendir (fn);
  if (d)
    {
      struct dirent *f = readdirdot (d);
      if (f)
	{
	  sprintf (fn + strlen (fn), "/%s", f->d_name);
	  sms_readfile (h, fn);
	  if (readdirdot (d))
	    more = 1;		/* more to send */
	}
      closedir (d);
    }
  if (*h->da || *h->oa)
    {				/* message to send */
      unsigned char p = 2;
      h->omsg[0] = 0x91;	/* SMS_DATA */
      if (h->smsc)
	{			/* deliver */
	  h->omsg[p++] = (more ? 4 : 0);
	  p += packaddress (h->omsg + p, h->oa);
	  h->omsg[p++] = h->pid;
	  h->omsg[p++] = h->dcs;
	  packdate (h->omsg + p, h->scts);
	  p += 7;
	  h->omsg[p++] = h->udl;
	  if (h->udl)
	    {
	      if (h->dcs & 4)
		{
		  memcpy (h->omsg + p, h->ud, h->udl);
		  p += h->udl;
		}
	      else
		p += pack7 (h->omsg + p, h->ud, h->udl);
	    }
	}
      else
	{			/* submit */
	  h->omsg[p++] = 0x01 + (more ? 4 : 0) + (h->srr ? 0x20 : 0) + (h->rp ? 0x80 : 0) + (h->vp ? 0x10 : 0);
	  h->omsg[p++] = h->mr;
	  p += packaddress (h->omsg + p, h->da);
	  h->omsg[p++] = h->pid;
	  h->omsg[p++] = h->dcs;
	  if (h->vp)
	    {			/* relative VP */
	      if (h->vp < 720)
		h->omsg[p++] = (h->vp + 4) / 5 - 1;
	      else if (h->vp < 1440)
		h->omsg[p++] = (h->vp - 720 + 29) / 30 + 143;
	      else if (h->vp < 43200)
		h->omsg[p++] = (h->vp + 1439) / 1440 + 166;
	      else if (h->vp < 635040)
		h->omsg[p++] = (h->vp + 10079) / 10080 + 192;
	      else
		h->omsg[p++] = 255;	/* max */
	    }
	  h->omsg[p++] = h->udl;
	  if (h->udl)
	    {
	      if (h->dcs & 4)
		{
		  memcpy (h->omsg + p, h->ud, h->udl);
		  p += h->udl;
		}
	      else
		p += pack7 (h->omsg + p, h->ud, h->udl);
	    }
	}
      h->omsg[1] = p - 2;
      sms_messagetx (h);
    }
  else
    {				/* no message */
      h->omsg[0] = 0x94;	/* SMS_REL */
      h->omsg[1] = 0;
      sms_messagetx (h);
    }
}

static void
sms_messagerx (sms_t * h)
{
  ast_verbose (VERBOSE_PREFIX_3 "SMS RX %02X %02X %02X %02X %02X %02X...\n", h->imsg[0], h->imsg[1], h->imsg[2],
	       h->imsg[3], h->imsg[4], h->imsg[5]);
  /* testing */
  switch (h->imsg[0])
    {
    case 0x91:			/* SMS_DATA */
      {
	unsigned char cause = sms_handleincoming (h);
	if (!cause)
	  {
	    sms_log (h, 'Y');
	    h->omsg[0] = 0x95;	/* SMS_ACK */
	    h->omsg[1] = 0x02;
	    h->omsg[2] = 0x00;	/* deliver report */
	    h->omsg[3] = 0x00;	/* no parameters */
	  }
	else
	  {			/* NACK */
	    sms_log (h, 'N');
	    h->omsg[0] = 0x96;	/* SMS_NACK */
	    h->omsg[1] = 3;
	    h->omsg[2] = 0;	/* delivery report */
	    h->omsg[3] = cause;	/* cause */
	    h->omsg[4] = 0;	/* no parameters */
	  }
	sms_messagetx (h);
      }
      break;
    case 0x92:			/* SMS_ERROR */
      sms_messagetx (h);	/* send whatever we sent again */
      break;
    case 0x93:			/* SMS_EST */
      sms_nextoutgoing (h);
      break;
    case 0x94:			/* SMS_REL */
      h->hangup = 1;		/* hangup */
      break;
    case 0x95:			/* SMS_ACK */
      sms_log (h, 'Y');
      sms_nextoutgoing (h);
      break;
    case 0x96:			/* SMS_NACK */
      sms_log (h, 'N');
      sms_nextoutgoing (h);
      break;
    default:			/* Unknown */
      h->omsg[0] = 0x92;	/* SMS_ERROR */
      h->omsg[1] = 1;
      h->omsg[2] = 3;		/* unknown message type; */
      sms_messagetx (h);
      break;
    }
}

static void
sms_messagetx (sms_t * h)
{
  unsigned char c = 0, p;
  for (p = 0; p < h->omsg[1] + 2; p++)
    c += h->omsg[p];
  h->omsg[h->omsg[1] + 2] = 0 - c;
  ast_verbose (VERBOSE_PREFIX_3 "SMS TX %02X %02X %02X %02X %02X %02X...\n", h->omsg[0], h->omsg[1], h->omsg[2],
	       h->omsg[3], h->omsg[4], h->omsg[5]);
  h->obyte = 1;
  h->opause = 200;
  if (h->omsg[0] == 0x93)
    h->opause = 2400;		/* initial message delay 300ms (for BT) */
  h->obytep = 0;
  h->obitp = 0;
  h->osync = 80;
  h->obyten = h->omsg[1] + 3;
}


static int
sms_generate (struct ast_channel *chan, void *data, int len, int samples)
{
  struct ast_frame f;
  unsigned char waste[AST_FRIENDLY_OFFSET];
  signed short buf[800];
  sms_t *h = data;
  int i;

  if (len > sizeof (buf))
    {
      ast_log (LOG_WARNING, "Only doing %d bytes (%d bytes requested)\n", sizeof (buf) / sizeof (signed short), len);
      len = sizeof (buf);
      samples = len / 2;
    }
  waste[0] = 0;			/* make compiler happy */
  f.frametype = AST_FRAME_VOICE;
  f.subclass = AST_FORMAT_SLINEAR;
  f.offset = AST_FRIENDLY_OFFSET;
  f.mallocd = 0;
  f.data = buf;
  f.datalen = samples * 2;
  f.samples = samples;
  f.src = "app_sms";
  /* create a buffer containing the digital sms pattern */
  for (i = 0; i < samples; i++)
    {
      buf[i] = 0;
      if (h->opause)
	h->opause--;
      else if (h->obyten || h->osync)
	{			/* sending data */
	  buf[i] = wave[h->ophase];
	  if ((h->ophase += ((h->obyte & 1) ? 13 : 21)) >= 80)
	    h->ophase -= 80;
	  if ((h->ophasep += 12) >= 80)
	    {			/* next bit */
	      h->ophasep -= 80;
	      if (h->osync)
		h->osync--;	/* sending sync bits */
	      else
		{
		  h->obyte >>= 1;
		  h->obitp++;
		  if (h->obitp == 1)
		    h->obyte = 0;	/* start bit; */
		  else if (h->obitp == 2)
		    h->obyte = h->omsg[h->obytep];
		  else if (h->obitp == 10)
		    {
		      h->obyte = 1;	/* stop bit */
		      h->obitp = 0;
		      h->obytep++;
		      if (h->obytep == h->obyten)
			{
			  h->obytep = h->obyten = 0;	/* sent */
			  h->osync = 10;	/* trailing marks */
			}
		    }
		}
	    }
	}
    }
  if (ast_write (chan, &f) < 0)
    {
      ast_log (LOG_WARNING, "Failed to write frame to '%s': %s\n", chan->name, strerror (errno));
      return -1;
    }
  return 0;
}

static void
sms_process (sms_t * h, int samples, signed short *data)
{
  if (h->obyten || h->osync)
    return;			/* sending */
  while (samples--)
    {
      unsigned long long m0, m1;
      if (abs (*data) > h->imag)
	h->imag = abs (*data);
      else
	h->imag = h->imag * 7 / 8;
      if (h->imag > 500)
	{
	  h->ims0 = (h->ims0 * 6 + *data * wave[h->ips0]) / 7;
	  h->idle = 0;
	  h->imc0 = (h->imc0 * 6 + *data * wave[h->ipc0]) / 7;
	  h->ims1 = (h->ims1 * 6 + *data * wave[h->ips1]) / 7;
	  h->imc1 = (h->imc1 * 6 + *data * wave[h->ipc1]) / 7;
	  m0 = h->ims0 * h->ims0 + h->imc0 * h->imc0;
	  m1 = h->ims1 * h->ims1 + h->imc1 * h->imc1;
	  if ((h->ips0 += 21) >= 80)
	    h->ips0 -= 80;
	  if ((h->ipc0 += 21) >= 80)
	    h->ipc0 -= 80;
	  if ((h->ips1 += 13) >= 80)
	    h->ips1 -= 80;
	  if ((h->ipc1 += 13) >= 80)
	    h->ipc1 -= 80;
	  {
	    char bit;
	    h->ibith <<= 1;
	    if (m1 > m0)
	      h->ibith |= 1;
	    if (h->ibith & 8)
	      h->ibitt--;
	    if (h->ibith & 1)
	      h->ibitt++;
	    bit = ((h->ibitt > 1) ? 1 : 0);
	    if (bit != h->ibitl)
	      h->ibitc = 1;
	    else
	      h->ibitc++;
	    h->ibitl = bit;
	    if (!h->ibitn && h->ibitc == 4 && !bit)
	      {
		h->ibitn = 1;
		h->iphasep = 0;
	      }
	    if (bit && h->ibitc == 200)
	      {			/* sync, restart message */
		h->ierr = h->ibitn = h->ibytep = h->ibytec = 0;
	      }
	    if (h->ibitn)
	      {
		h->iphasep += 12;
		if (h->iphasep >= 80)
		  {		/* next bit */
		    h->iphasep -= 80;
		    if (h->ibitn++ == 9)
		      {		/* end of byte */
			if (!bit)	/* bad stop bit */
			  h->ierr = 0xFF;	/* unknown error */
			else
			  {
			    if (h->ibytep < sizeof (h->imsg))
			      {
				h->imsg[h->ibytep] = h->ibytev;
				h->ibytec += h->ibytev;
				h->ibytep++;
			      }
			    else if (h->ibytep == sizeof (h->imsg))
			      h->ierr = 2;	/* bad message length */
			    if (h->ibytep > 1 && h->ibytep == 3 + h->imsg[1] && !h->ierr)
			      {
				if (!h->ibytec)
				  sms_messagerx (h);
				else
				  h->ierr = 1;	/* bad checksum */
			      }
			  }
			h->ibitn = 0;
		      }
		    h->ibytev = (h->ibytev >> 1) + (bit ? 0x80 : 0);
		  }
	      }
	  }
	}
      else
	{			/* lost carrier */
	  if (h->idle++ == 80000)
	    {			/* nothing happening */
	      ast_log (LOG_EVENT, "No data, hanging up\n");
	      h->hangup = 1;
	    }
	  if (h->ierr)
	    {			/* error */
	      h->omsg[0] = 0x92;	/* error */
	      h->omsg[1] = 1;
	      h->omsg[2] = h->ierr;
	      sms_messagetx (h);	/* send error */
	    }
	  h->ierr = h->ibitn = h->ibytep = h->ibytec = 0;
	}
      data++;
    }
}

static struct ast_generator smsgen = {
alloc:sms_alloc,
release:sms_release,
generate:sms_generate,
};

static int
sms_exec (struct ast_channel *chan, void *data)
{
  int res = -1;
  struct localuser *u;
  struct ast_frame *f;
  sms_t h = { 0 };
  h.ipc0 = h.ipc1 = 20;		/* phase for cosine */
  h.dcs = 0xF1;			/* default */
  if (!data)
    {
      ast_log (LOG_ERROR, "Requires queue name at least\n");
      return -1;
    }

  if (chan->callerid)
    {				/* get caller ID. Used as originating address on sc side receives */
      char temp[256], *name, *num;
      strncpy (temp, chan->callerid, sizeof (temp));
      ast_callerid_parse (temp, &name, &num);
      if (!num)
	num = temp;
      ast_shrink_phone_number (num);
      if (strlen (num) < sizeof (h.cli))
	strcpy (h.cli, num);
    }

  {
    char *d = data, *p, answer = 0;
    if (!*d || *d == '|')
      {
	ast_log (LOG_ERROR, "Requires queue name\n");
	return -1;
      }
    for (p = d; *p && *p != '|'; p++);
    if (p - d >= sizeof (h.queue))
      {
	ast_log (LOG_ERROR, "Queue name too long\n");
	return -1;
      }
    strncpy (h.queue, d, p - d);
    if (*p == '|')
      p++;
    d = p;
    for (p = h.queue; *p; p++)
      if (!isalnum (*p))
	*p = '-';		/* make very safe for filenames */
    while (*d && *d != '|')
      {
	switch (*d)
	  {
	  case 'a':		/* we have to send the initial FSK sequence */
	    answer = 1;
	    break;
	  case 's':		/* we are acting as a service centre talking to a phone */
	    h.smsc = 1;
	    break;
	    /* the following apply if there is an arg3/4 and apply to the created message file */
	  case 'r':
	    h.srr = 1;
	    break;
	  case 'o':
	    h.dcs |= 4;		/* octets */
	    break;
	  case '1':
	  case '2':
	  case '3':
	  case '4':
	  case '5':
	  case '6':
	  case '7':		/* set the pid for saved local message */
	    h.pid = 0x40 + (*d & 0xF);
	    break;
	  }
	d++;
      }
    if (*d == '|')
      {				/* submitting a message, not taking call. */
	d++;
	h.scts = time (0);
	for (p = d; *p && *p != '|'; p++);
	if (*p)
	  *p++ = 0;
	if (strlen (d) >= sizeof (h.oa))
	  {
	    ast_log (LOG_ERROR, "Address too long %s\n", d);
	    return 0;
	  }
	strcpy (h.smsc ? h.oa : h.da, d);
	if (!h.smsc)
	  strcpy (h.oa, h.cli);
	d = p;
	if (!(h.dcs & 4) && check7 (h.udl, h.ud))
	  ast_log (LOG_WARNING, "Invalid GSM characters in %.*s\n", h.udl, h.ud);
	if (strlen (d) > ((h.dcs & 4) ? 140 : 160))
	  {
	    ast_log (LOG_ERROR, "Message too long %s\n", d);
	    h.udl = ((h.dcs & 4) ? 140 : 160);
	  }
	else
	  h.udl = strlen (d);
	if (h.udl)
	  memcpy (h.ud, d, h.udl);
	h.smsc = !h.smsc;	/* file woul go in wrong directory otherwise... */
	sms_writefile (&h);
	return 0;
      }

    if (answer)
      {				/* set up SMS_EST initial message */
	h.omsg[0] = 0x93;
	h.omsg[1] = 0;
	sms_messagetx (&h);
      }
  }

  LOCAL_USER_ADD (u);
  if (chan->_state != AST_STATE_UP)
    ast_answer (chan);

  res = ast_set_write_format (chan, AST_FORMAT_SLINEAR);
  if (res >= 0)
    res = ast_set_read_format (chan, AST_FORMAT_SLINEAR);
  if (res < 0)
    {
      LOCAL_USER_REMOVE (u);
      ast_log (LOG_ERROR, "Unable to set to linear mode, giving up\n");
      return -1;
    }

  if (ast_activate_generator (chan, &smsgen, &h) < 0)
    {
      LOCAL_USER_REMOVE (u);
      ast_log (LOG_ERROR, "Failed to activate generator on '%s'\n", chan->name);
      return -1;
    }

  /* Do our thing here */
  while (ast_waitfor (chan, -1) > -1 && !h.hangup)
    {
      f = ast_read (chan);
      if (!f)
	break;
      if (f->frametype == AST_FRAME_VOICE)
	{
	  sms_process (&h, f->samples, f->data);
	}

      ast_frfree (f);
    }

  sms_log (&h, '?');		/* log incomplete message */

  LOCAL_USER_REMOVE (u);
  return h.hangup;
}

int
unload_module (void)
{
  STANDARD_HANGUP_LOCALUSERS;
  return ast_unregister_application (app);
}

int
load_module (void)
{
  {				/* fill in sms8to7 from sms7to8 */
    int p;
    for (p = 0; p < 256; p++)
      sms8to7[p] = 0xE0;	/* inverted question mark and invalid */
    for (p = 0; p < 128; p++)
      sms8to7[sms7to8[p]] = p;
  }
  return ast_register_application (app, sms_exec, synopsis, descrip);
}

char *
description (void)
{
  return tdesc;
}

int
usecount (void)
{
  int res;
  STANDARD_USECOUNT (res);
  return res;
}

char *
key ()
{
  return ASTERISK_GPL_KEY;
}
