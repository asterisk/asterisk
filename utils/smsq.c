#include <stdio.h>
#include <popt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <asterisk/compat.h>
#ifdef SOLARIS
#define     POPT_ARGFLAG_SHOW_DEFAULT 0x00800000
#endif
#if !defined(POPT_ARGFLAG_SHOW_DEFAULT)
#define     POPT_ARGFLAG_SHOW_DEFAULT 0x00800000
#endif

/* SMS queuing application for use with asterisk app_sms */
/* by Adrian Kennard, 2004 - 2005 */

/* reads next USC character from null terminated UTF-8 string and advanced pointer */
/* for non valid UTF-8 sequences, returns character as is */
/* Does not advance pointer for null termination */
static int utf8decode (unsigned char **pp)
{
   unsigned char *p = *pp;
   if (!*p)
      return 0;                 /* null termination of string */
   (*pp)++;
   if (*p < 0xC0)
      return *p;                /* ascii or continuation character */
   if (*p < 0xE0)
   {
      if (*p < 0xC2 || (p[1] & 0xC0) != 0x80)
         return *p;             /* not valid UTF-8 */
      (*pp)++;
      return ((*p & 0x1F) << 6) + (p[1] & 0x3F);
   }
   if (*p < 0xF0)
   {
      if ((*p == 0xE0 && p[1] < 0xA0) || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80)
         return *p;             /* not valid UTF-8 */
      (*pp) += 2;
      return ((*p & 0x0F) << 12) + ((p[1] & 0x3F) << 6) + (p[2] & 0x3F);
   }
   if (*p < 0xF8)
   {
      if ((*p == 0xF0 && p[1] < 0x90) || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80)
         return *p;             /* not valid UTF-8 */
      (*pp) += 3;
      return ((*p & 0x07) << 18) + ((p[1] & 0x3F) << 12) + ((p[2] & 0x3F) << 6) + (p[3] & 0x3F);
   }
   if (*p < 0xFC)
   {
      if ((*p == 0xF8 && p[1] < 0x88) || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80
          || (p[4] & 0xC0) != 0x80)
         return *p;             /* not valid UTF-8 */
      (*pp) += 4;
      return ((*p & 0x03) << 24) + ((p[1] & 0x3F) << 18) + ((p[2] & 0x3F) << 12) + ((p[3] & 0x3F) << 6) + (p[4] & 0x3F);
   }
   if (*p < 0xFE)
   {
      if ((*p == 0xFC && p[1] < 0x84) || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80
          || (p[4] & 0xC0) != 0x80 || (p[5] & 0xC0) != 0x80)
         return *p;             /* not valid UTF-8 */
      (*pp) += 5;
      return ((*p & 0x01) << 30) + ((p[1] & 0x3F) << 24) + ((p[2] & 0x3F) << 18) + ((p[3] & 0x3F) << 12) + ((p[4] & 0x3F) << 6) +
         (p[5] & 0x3F);
   }
   return *p;                   /* not sensible */
}

/* check for any queued messages in specific queue (queue="" means any queue) */
/* returns 0 if nothing queued, 1 if queued and outgoing set up OK, 2 of outgoing exists */
static char txqcheck (char *dir, char *queue, char subaddress, char *channel, char *callerid, int wait, int delay, int retries, int concurrent)
{
   char ogname[100],
     temp[100],
     dirname[100],
    *p=NULL;
   FILE *f;
   DIR *d;
   int ql = strlen (queue), qfl = ql;
   struct dirent *fn;
   snprintf (dirname, sizeof(dirname), "sms/%s", dir);
   d = opendir (dirname);
   if (!d)
      return 0;
   while ((fn = readdir (d))
          && !(*fn->d_name != '.'
               && ((!ql && (p = strchr (fn->d_name, '.'))) || (ql && !strncmp (fn->d_name, queue, ql) && fn->d_name[ql] == '.'))));
   if (!fn)
   {
      closedir (d);
      return 0;
   }
   if (!ql)
   {                            /* not searching any specific queue, so use whatr we found as the queue */
      queue = fn->d_name;
      qfl = ql = p - queue;
   }
   p = strchr (queue, '-');
   if (p && p < queue + ql)
   {
      ql = p - queue;
      subaddress = p[1];
   }
   snprintf (temp, sizeof(temp), "sms/.smsq-%d", getpid ());
   f = fopen (temp, "w");
   if (!f)
   {
      perror (temp);
      closedir (d);
      return 0;
   }
   fprintf (f, "Channel: ");
   if (!channel)
      fprintf (f, "Local/%.*s\n", ql, queue);
   else
   {
      p = strchr (channel, '/');
      if (!p)
         p = channel;
      p = strchr (p, 'X');
      if (p)
         fprintf (f, "%.*s%c%s\n", p - channel, channel, subaddress, p + 1);
      else
         fprintf (f, "%s\n", channel);
   }
   fprintf (f, "Callerid: SMS <");
   if (!callerid)
      fprintf (f, "%.*s", ql, queue);
   else
   {
      p = strchr (callerid, 'X');
      if (p)
         fprintf (f, "%.*s%c%s", p - callerid, callerid, subaddress, p + 1);
      else
         fprintf (f, "%s", callerid);
   }
   fprintf (f, ">\n");
   fprintf (f, "Application: SMS\n");
   fprintf (f, "Data: %.*s", qfl, queue);
   if (dir[1] == 't')
      fprintf (f, "|s");
   fprintf (f, "\nMaxRetries: %d\n", retries);
   fprintf (f, "RetryTime: %d\n", delay);
   fprintf (f, "WaitTime: %d\n", wait);
   fclose (f);
   closedir (d);
   {
      int try = 0;
      while (try < concurrent)
      {
         try++;
         snprintf(ogname, sizeof(ogname), "outgoing/smsq.%s.%s.%d", dir, queue, try);
         if (!link (temp, ogname))
         {                      /* queued OK */
            unlink (temp);
            return 1;
         }
      }
   }
   /* failed to create call queue */
   unlink (temp);
   return 2;
}

/* Process received queue entries and run through a process, setting environment variables */
static void rxqcheck (char *dir, char *queue, char *process)
{
   unsigned char *p;
   char dirname[100],
     temp[100];
   DIR *d;
   int ql = strlen (queue);
   struct dirent *fn;
   snprintf(temp, sizeof(temp), "sms/.smsq-%d", getpid ());
   snprintf(dirname, sizeof(dirname), "sms/%s", dir);
   d = opendir (dirname);
   if (!d)
      return;
   while ((fn = readdir (d)))
      if ((*fn->d_name != '.'
           && ((!ql && (p = strchr (fn->d_name, '.'))) || (ql && !strncmp (fn->d_name, queue, ql) && fn->d_name[ql] == '.'))))
      {                         /* process file */
         char filename[1010];
         char line[1000];
         unsigned short ud[160];
         unsigned char udl = 0;
         FILE *f;
         snprintf (filename, sizeof(filename), "sms/%s/%s", dir, fn->d_name);
         if (rename (filename, temp))
            continue;           /* cannot access file */
         f = fopen (temp, "r");
         unlink (temp);
         if (!f)
         {
            perror (temp);
            continue;
         }
         unsetenv ("oa");
         unsetenv ("da");
         unsetenv ("scts");
         unsetenv ("pid");
         unsetenv ("dcs");
         unsetenv ("mr");
         unsetenv ("srr");
         unsetenv ("rp");
         unsetenv ("vp");
         unsetenv ("udh");
         unsetenv ("ud");
         unsetenv ("ude");
         unsetenv ("ud8");
         unsetenv ("ud16");
         unsetenv ("morx");
         unsetenv ("motx");
         unsetenv ("queue");
         if (*queue)
            setenv ("queue", queue, 1);
         setenv (dir, "", 1);
         while (fgets (line, sizeof (line), f))
         {
            for (p = line; *p && *p != '\n' && *p != '\r'; p++);
            *p = 0;             /* strip eoln */
            p = line;
            if (!*p || *p == ';')
               continue;        /* blank line or comment, ignore */
            while (isalnum (*p))
            {
               *p = tolower (*p);
               p++;
            }
            while (isspace (*p))
               *p++ = 0;
            if (*p == '=')
            {                   /* = */
               *p++ = 0;
               if (!strcmp (line, "oa") || !strcmp (line, "da") || !strcmp (line, "scts") || !strcmp (line, "pid")
                   || !strcmp (line, "dcs") || !strcmp (line, "mr") || !strcmp (line, "vp"))
                  setenv (line, p, 1);
               else if ((!strcmp (line, "srr") || !strcmp (line, "rp")) && atoi (p))
                  setenv (line, "", 1);
               else if (!strcmp (line, "ud"))
               {                /* read the user data as UTF-8 */
                  long v;
                  udl = 0;
                  while ((v = utf8decode (&p)) && udl < 160)
                     if (v && v <= 0xFFFF)
                        ud[udl++] = v;
               }
            } else if (*p == '#')
            {
               *p++ = 0;
               if (*p == '#')
               {                /* ##  */
                  p++;
                  if (!strcmp (line, "udh"))
                     setenv (line, p, 1);
                  else if (!strcmp (line, "ud"))
                  {             /* read user data UCS-2 */
                     udl = 0;
                     while (*p && udl < 160)
                     {
                        if (isxdigit (*p) && isxdigit (p[1]) && isxdigit (p[2]) && isxdigit (p[3]))
                        {
                           ud[udl++] =
                              (((isalpha (*p) ? 9 : 0) + (*p & 0xF)) << 12) +
                              (((isalpha (p[1]) ? 9 : 0) + (p[1] & 0xF)) << 8) +
                              (((isalpha (p[2]) ? 9 : 0) + (p[2] & 0xF)) << 4) + ((isalpha (p[3]) ? 9 : 0) + (p[3] & 0xF));
                           p += 4;
                        } else
                           break;
                     }
                  }
               } else
               {                /* # */
                  if (!strcmp (line, "ud"))
                  {             /* read user data UCS-1 */
                     udl = 0;
                     while (*p && udl < 160)
                     {
                        if (isxdigit (*p) && isxdigit (p[1]))
                        {
                           ud[udl++] = (((isalpha (*p) ? 9 : 0) + (*p & 0xF)) << 4) + ((isalpha (p[1]) ? 9 : 0) + (p[1] & 0xF));
                           p += 2;
                        } else
                           break;
                     }
                  }
               }
            }
         }
         fclose (f);
         /* set up user data variables */
         {
            char temp[481];
            int n,
              p;
            for (n = 0, p = 0; p < udl; p++)
            {
               unsigned short v = ud[p];
               if (v)
               {
                  if (v < 0x80)
                     temp[n++] = v;
                  else if (v < 0x800)
                  {
                     temp[n++] = (0xC0 + (v >> 6));
                     temp[n++] = (0x80 + (v & 0x3F));
                  } else
                  {
                     temp[n++] = (0xE0 + (v >> 12));
                     temp[n++] = (0x80 + ((v >> 6) & 0x3F));
                     temp[n++] = (0x80 + (v & 0x3F));
                  }
               }
            }
            temp[n] = 0;
            setenv ("ud", temp, 1);
            for (n = 0, p = 0; p < udl; p++)
            {
               unsigned short v = ud[p];
               if (v < ' ' || v == '\\')
               {
                  temp[n++] = '\\';
                  if (v == '\\')
                     temp[n++] = '\\';
                  else if (v == '\n')
                     temp[n++] = 'n';
                  else if (v == '\r')
                     temp[n++] = 'r';
                  else if (v == '\t')
                     temp[n++] = 't';
                  else if (v == '\f')
                     temp[n++] = 'f';
                  else
                  {
                     temp[n++] = '0' + (v >> 6);
                     temp[n++] = '0' + ((v >> 3) & 7);
                     temp[n++] = '0' + (v & 7);
                  }
               } else if (v < 0x80)
                  temp[n++] = v;
               else if (v < 0x800)
               {
                  temp[n++] = (0xC0 + (v >> 6));
                  temp[n++] = (0x80 + (v & 0x3F));
               } else
               {
                  temp[n++] = (0xE0 + (v >> 12));
                  temp[n++] = (0x80 + ((v >> 6) & 0x3F));
                  temp[n++] = (0x80 + (v & 0x3F));
               }
            }
            temp[n] = 0;
            setenv ("ude", temp, 1);
            for (p = 0; p < udl && ud[p] < 0x100; p++);
            if (p == udl)
            {
               for (n = 0, p = 0; p < udl; p++)
               {
                  sprintf (temp + n, "%02X", ud[p]);
                  n += 2;
               }
               setenv ("ud8", temp, 1);
            }
            for (n = 0, p = 0; p < udl; p++)
            {
               sprintf (temp + n, "%04X", ud[p]);
               n += 4;
            }
            setenv ("ud16", temp, 1);
         }
         /* run the command */
         system (process);
      }
   closedir (d);
}

/* Main app */
int
main (int argc, const char *argv[])
{
   char c;
   int mt = 0,
      mo = 0,
      tx = 0,
      rx = 0,
      nodial = 0,
      nowait = 0,
      concurrent = 1,
      motxwait = 10,
      motxdelay = 1,
      motxretries = 10,
      mttxwait = 10,
      mttxdelay = 30,
      mttxretries = 100,
      mr = -1,
      pid = -1,
      dcs = -1,
      srr = 0,
      rp = 0,
      vp = 0,
      udl = 0,
      utf8 = 0,
      ucs1 = 0,
      ucs2 = 0;
   unsigned short ud[160];
   unsigned char *uds = 0,
      *udh = 0;
   char *da = 0,
      *oa = 0,
      *queue = "",
      *udfile = 0,
      *process = 0,
      *spooldir = "/var/spool/asterisk",
      *motxchannel = "Local/1709400X",
      *motxcallerid = 0,
      *mttxchannel = 0,
      *mttxcallerid = "080058752X0",
      *defaultsubaddress = "9",
      subaddress = 0,
      *scts = 0;
   poptContext optCon;          /* context for parsing command-line options */
   const struct poptOption optionsTable[] = {
      {"queue", 'q', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &queue, 0, "Queue [inc sub address]", "number[-X]"},
      {"da", 'd', POPT_ARG_STRING, &da, 0, "Destination address", "number"},
      {"oa", 'o', POPT_ARG_STRING, &oa, 0, "Origination address", "number"},
      {"ud", 'm', POPT_ARG_STRING, &uds, 0, "Message", "text"},
      {"ud-file", 'f', POPT_ARG_STRING, &udfile, 0, "Message file", "filename"},
      {"UTF-8", 0, POPT_ARG_NONE, &utf8, 0, "File treated as null terminated UTF-8 (default)", 0},
      {"UCS-1", 0, POPT_ARG_NONE, &ucs1, 0, "File treated as UCS-1", 0},
      {"UCS-2", 0, POPT_ARG_NONE, &ucs2, 0, "File treated as UCS-2", 0},
      {"mt", 't', POPT_ARG_NONE, &mt, 0, "Mobile Terminated", 0},
      {"mo", 0, POPT_ARG_NONE, &mo, 0, "Mobile Originated", 0},
      {"tx", 0, POPT_ARG_NONE, &tx, 0, "Send message", 0},
      {"rx", 'r', POPT_ARG_NONE, &rx, 0, "Queue for receipt", 0},
      {"process", 'e', POPT_ARG_STRING, &process, 0, "Rx queue process command", "command"},
      {"no-dial", 'x', POPT_ARG_NONE, &nodial, 0, "Do not dial", 0},
      {"no-wait", 0, POPT_ARG_NONE, &nowait, 0, "Do not wait if already calling", 0},
      {"concurrent", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &concurrent, 0, "Number of concurrent calls to allow", "n"},
      {"motx-channel", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &motxchannel, 0, "Channel for motx calls", "channel"},
      {"motx-callerid", 0, POPT_ARG_STRING, &motxcallerid, 0,
       "Caller ID for motx calls (default is queue name without sub address)", "number"},
      {"motx-wait", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &motxwait, 0, "Time to wait for motx call to answer",
       "seconds"},
      {"motx-delay", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &motxdelay, 0, "Time between motx call retries", "seconds"},
      {"motx-retries", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &motxretries, 0, "Number of retries for motx call", "n"},
      {"mttx-channel", 0, POPT_ARG_STRING, &mttxchannel, 0,
       "Channel for mttx calls (default is Local/ and queue name without sub address)", "channel"},
      {"mttx-callerid", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &mttxcallerid, 0,
       "Caller ID for mttx calls (default is queue name without sub address)", "number"},
      {"mttx-wait", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &mttxwait, 0, "Time to wait for mttx call to answer",
       "seconds"},
      {"mttx-delay", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &mttxdelay, 0, "Time between mttx call retries", "seconds"},
      {"mttx-retries", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &mttxretries, 0, "Number of retries for mttx call", "n"},
      {"mr", 'n', POPT_ARG_INT, &mr, 0, "Message reference", "n"},
      {"pid", 'p', POPT_ARG_INT, &pid, 0, "Protocol ID", "n"},
      {"dcs", 'c', POPT_ARG_INT, &dcs, 0, "Data Coding Scheme", "n"},
      {"udh", 0, POPT_ARG_STRING, &udh, 0, "User data header", "hex"},
      {"srr", 0, POPT_ARG_NONE, &srr, 0, "Status Report Request", 0},
      {"rp", 0, POPT_ARG_NONE, &rp, 0, "Return Path request", 0},
      {"v", 0, POPT_ARG_INT, &vp, 0, "Validity Period", "seconds"},
      {"scts", 0, POPT_ARG_STRING, &scts, 0, "Timestamp", "YYYY-MM-SSTHH:MM:SS"},
      {"default-sub-address", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &defaultsubaddress, 0, "Default sub address", "X"},
      {"spool-dir", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &spooldir, 0, "Asterisk spool dir", "dirname"},
      POPT_AUTOHELP {NULL, 0, 0, NULL, 0}
   };

   optCon = poptGetContext (NULL, argc, argv, optionsTable, 0);
   poptSetOtherOptionHelp (optCon, "<oa/da> <message>");

   /* Now do options processing, get portname */
   if ((c = poptGetNextOpt (optCon)) < -1)
   {
      /* an error occurred during option processing */
      fprintf (stderr, "%s: %s\n", poptBadOption (optCon, POPT_BADOPTION_NOALIAS), poptStrerror (c));
      return 1;
   }
   if (!ucs1 && !ucs2)
      utf8 = 1;
   if (utf8 + ucs1 + ucs2 > 1)
   {
      fprintf (stderr, "Pick one of UTF-8, UCS-1 or UCS-2 only\n");
      return 1;
   }
   if (!udfile && (ucs1 || ucs2))
   {
      fprintf (stderr, "Command line arguments always treated as UTF-8\n");
      return 1;
   }
   /*  if (!where && poptPeekArg (optCon)) where = (char *) poptGetArg (optCon); */
   if (!mt && !mo && process)
      mt = 1;
   if (!mt && !mo && oa)
      mt = 1;
   if (!mt)
      mo = 1;
   if (mt && mo)
   {
      fprintf (stderr, "Cannot be --mt and --mo\n");
      return 1;
   }
   if (!rx && !tx && process)
      rx = 1;
   if (!rx)
      tx = 1;
   if (tx && rx)
   {
      fprintf (stderr, "Cannot be --tx and --rx\n");
      return 1;
   }
   if (rx)
      nodial = 1;
   if (uds && udfile)
   {
      fprintf (stderr, "Cannot have --ud and --ud-file\n");
      return 1;
   }
   if (mo && !da && poptPeekArg (optCon))
      da = (char *) poptGetArg (optCon);
   if (mt && !oa && poptPeekArg (optCon))
      oa = (char *) poptGetArg (optCon);
   if (tx && oa && mo)
   {
      fprintf (stderr, "--oa makes no sense with --mo as CLI is used (i.e. queue name)\n");
      return 1;
   }
   if (tx && da && mt)
   {
      fprintf (stderr, "--da makes no sense with --mt as called number is used (i.e. queue name)\n");
      return 1;
   }
   if (da && strlen (da) > 20)
   {
      fprintf (stderr, "--da too long\n");
      return 1;
   }
   if (oa && strlen (oa) > 20)
   {
      fprintf (stderr, "--oa too long\n");
      return 1;
   }
   if (queue && strlen (queue) > 20)
   {
      fprintf (stderr, "--queue name too long\n");
      return 1;
   }
   if (mo && scts)
   {
      fprintf (stderr, "scts is set my service centre\n");
      return 1;
   }
   if (uds)
   {                            /* simple user data command line option in \UTF-8 */
      while (udl < 160 && *uds)
      {
         int v = utf8decode (&uds);
         if (v > 0xFFFF)
         {
            fprintf (stderr, "Invalid character U+%X at %d\n", v, udl);
            return 1;
         }
         ud[udl++] = v;
      }
   }
   if (!uds && !udfile && poptPeekArg (optCon))
   {                            /* multiple command line arguments in UTF-8 */
      while (poptPeekArg (optCon) && udl < 160)
      {
         unsigned char *a = (char *) poptGetArg (optCon);
         if (udl && udl < 160)
            ud[udl++] = ' ';    /* space between arguments */
         while (udl < 160 && *a)
         {
            int v = utf8decode (&a);
            if (v > 0xFFFF)
            {
               fprintf (stderr, "Invalid character U+%X at %d\n", v, udl);
               return 1;
            }
            ud[udl++] = v;
         }
      }
   }
   if (poptPeekArg (optCon))
   {
      fprintf (stderr, "Unknown argument %s\n", poptGetArg (optCon));
      return 1;
   }
   if (udfile)
   {                            /* get message from file */
      unsigned char dat[1204],
       *p = dat,
         *e;
      int f,
        n;
      if (*udfile)
         f = open (udfile, O_RDONLY);
      else
         f = fileno (stdin);
      if (f < 0)
      {
         perror (udfile);
         return 1;
      }
      n = read (f, dat, sizeof (dat));
      if (n < 0)
      {
         perror (udfile);
         return 1;
      }
      if (*udfile)
         close (f);
      e = dat + n;
      if (utf8)
      {                         /* UTF-8 */
         while (p < e && udl < 160 && *p)
            ud[udl++] = utf8decode (&p);
      } else if (ucs1)
      {                         /* UCS-1 */
         while (p < e && udl < 160)
            ud[udl++] = *p++;
      } else
      {                         /* UCS-2 */
         while (p + 1 < e && udl < 160)
         {
            ud[udl++] = (*p << 8) + p[1];
            p += 2;
         }
      }
   }
   if (queue)
   {
      char *d = strrchr (queue, '-');
      if (d && d[1])
         subaddress = d[1];
      else
         subaddress = *defaultsubaddress;
   }

   if (chdir (spooldir))
   {
      perror (spooldir);
      return 1;
   }

   if (oa || da)
   {                            /* send message */
      char temp[100],
        queuename[100],
       *dir = (mo ? rx ? "sms/morx" : "sms/motx" : rx ? "sms/mtrx" : "sms/mttx");
      FILE *f;
      snprintf (temp, sizeof(temp), "sms/.smsq-%d", getpid ());
      mkdir ("sms", 0777);      /* ensure directory exists */
      mkdir (dir, 0777);        /* ensure directory exists */
      snprintf (queuename, sizeof(queuename), "%s/%s.%ld-%d", dir, *queue ? queue : "0", (long)time (0), getpid ());
      f = fopen (temp, "w");
      if (!f)
      {
         perror (temp);
         return 1;
      }
      if (oa)
         fprintf (f, "oa=%s\n", oa);
      if (da)
         fprintf (f, "da=%s\n", da);
      if (scts)
         fprintf (f, "scts=%s\n", scts);
      if (pid >= 0)
         fprintf (f, "pid=%d\n", pid);
      if (dcs >= 0)
         fprintf (f, "dcs=%d\n", dcs);
      if (mr >= 0)
         fprintf (f, "mr=%d\n", mr);
      if (srr)
         fprintf (f, "srr=1\n");
      if (rp)
         fprintf (f, "rp=1\n");
      if (udh)
         fprintf (f, "udh#%s\n", udh);
      if (vp > 0)
         fprintf (f, "vp=%d\n", vp);
      if (udl)
      {
         int p;
         for (p = 0; p < udl && ud[p] < 0x100; p++);
         if (p == udl)
         {
            for (p = 0; p < udl && ud[p] < 0x80 && ud[p] >= 0x20; p++);
            if (p == udl)
            {                   /* use text */
               fprintf (f, "ud=");
               for (p = 0; p < udl; p++)
                  fputc (ud[p], f);
            } else
            {                   /* use one byte hex */
               fprintf (f, "ud#");
               for (p = 0; p < udl; p++)
                  fprintf (f, "%02X", ud[p]);
            }
         } else
         {                      /* use two byte hex */
            fprintf (f, "ud##");
            for (p = 0; p < udl; p++)
               fprintf (f, "%04X", ud[p]);
         }
         fprintf (f, "\n");
      }
      fclose (f);
      if (rename (temp, queuename))
      {
         perror (queuename);
         unlink (temp);
         return 1;
      }
   }

   if (!nodial && tx && !process)
   {                            /* dial to send messages */
      char ret=0,
        try = 3;
      if (nowait)
         try = 1;
      while (try--)
      {
         if (mo)
            ret = txqcheck ("motx", queue, subaddress, motxchannel, motxcallerid, motxwait, motxdelay, motxretries, concurrent);
         else
            ret = txqcheck ("mttx", queue, subaddress, mttxchannel, mttxcallerid, mttxwait, mttxdelay, mttxretries, concurrent);
         if (ret < 2)
            break;              /* sent, or queued OK */
         if (try)
            sleep (1);
      }
      if (ret == 2 && !nowait)
         fprintf (stderr, "No call scheduled as already sending\n");
   }
   if (process)
      rxqcheck (mo ? rx ? "morx" : "motx" : rx ? "mtrx" : "mttx", queue, process);

   return 0;
}
