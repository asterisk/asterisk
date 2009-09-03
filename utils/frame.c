/****************************************************************************
 *
 * Programs for processing sound files in raw- or WAV-format.
 * -- Useful functions for parsing command line options and
 *    issuing errors, warnings, and chit chat.
 *
 * Name:    frame.c
 * Version: see static char *standardversion, below.
 * Author:  Mark Roberts <mark@manumark.de>
 *	    Michael Labuschke <michael@labuschke.de> sys_errlist fixes 
 *		
 ****************************************************************************/
/****************************************************************************
 *  These are useful functions that all DSP programs might find handy
 ****************************************************************************/

#include <stdio.h>
#include <math.h>
#include <stdlib.h> /* for exit and malloc */
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include "frame.h"

time_t stopwatch;       /* will hold time at start of calculation */
int samplefrequency;
unsigned short samplewidth;
unsigned short channels;
int wavout;            /* TRUE iff out file should be a .WAV file */
int iswav;             /* TRUE iff in file was found to be a .WAV file */
FILE *in, *out;
char *infilename, *outfilename;
int verboselevel;
char *version = "";
char *usage = "";
static int test_usage;

static char *standardversion = "frame version 1.3, June 13th 2001";
static char *standardusage =
"\nOptions common to all mark-dsp programs:\n"

"-h \t\t create a WAV-header on output files.\n"
"-c#\t\t set number of channels to # (1 or 2). Default: like input.\n"
"-w#\t\t set number of bits per sample (width) to # (only 16)\n"
"-f#\t\t set sample frequency to #. Default: like input.\n"
"-V \t\t verbose: talk a lot.\n"
"-Q \t\t quiet: talk as little as possible.\n\n"
"In most cases, a filename of '-' means stdin or stdout.\n\n"
"Bug-reports: mark@manumark.de\n"
;

/* -----------------------------------------------------------------------
   Writes the number of samples to result that are yet to be read from anyin.
   Return values are TRUE on success, FALSE on failure.
   -----------------------------------------------------------------------*/
int getremainingfilelength( FILE *anyin, long *result)
{
    long i;

    i = ftell(anyin);
    if (i == -1) return FALSE;
    if (fseek(anyin, 0, SEEK_END) == -1) return FALSE;
    *result = ftell(anyin);
    if (*result == -1) return FALSE;
    (*result) -= i;
    (*result) /= samplewidth;
    if (fseek(anyin, i, SEEK_SET) == -1) return FALSE;
    return TRUE;
}

/* -----------------------------------------------------------------------
   Read a .pk-header from 'anyin'.
   -----------------------------------------------------------------------*/
void readpkheader( FILE *anyin)
{
   unsigned short tempushort;
   int tempint, i, x;
   unsigned char blood[8];

   for (i = 0; i < 11; i++)
   {
	   if (!fread( &tempint, 4, 1, anyin)) {
		   return;
	   }
	   printf( "%d: %d, ", i, tempint);
   }
   printf( "\n");
   if (!fread( blood, 1, 8, anyin)) {
	   return;
   }
   for (i = 0; i < 8; i++)
	   printf( "%d ", blood[i]);
   printf( "\n");
   for (i = 0; i < 8; i++)
   {
	   for (x = 128; x > 0; x /= 2)
		   printf((blood[i] & x) == 0? "0 ":"1 ");
	   printf(i%4==3? "\n":"| ");
   }
   printf( "\n");
   for (i = 0; i < 2; i++)
   {
	   if (!fread( &tempint, 4, 1, anyin)) {
		   return;
	   }
	   printf( "%d: %d, ", i, tempint);
   }
   printf( "\n");
   for (i = 0; i < 2; i++)
   {
	   if (!fread( &tempushort, 2, 1, anyin)) {
		   return;
	   }
	   printf( "%d: %d, ", i, tempushort);
   }
   printf( "\n");
}



/* -----------------------------------------------------------------------
   Read a .WAV header from 'anyin'. See header for details.
   -----------------------------------------------------------------------*/
void readwavheader( FILE *anyin)
{
   unsigned int tempuint, sf;
   unsigned short tempushort, cn;
   char str[9];
   int nowav = FALSE;

   iswav = FALSE;

   if (ftell(anyin) == -1) /* If we cannot seek this file */
   {
	   nowav = TRUE;   /* -> Pretend this is no wav-file */
	   chat("File not seekable: not checking for WAV-header.\n");
   }
   else
   {
	   /* Expect four bytes "RIFF" and four bytes filelength */
	   if (!fread(str, 1, 8, anyin)) {           /* 0 */
		   return;
	   }
	   str[4] = '\0';
	   if (strcmp(str, "RIFF") != 0) nowav = TRUE;
	   /* Expect eight bytes "WAVEfmt " */
	   if (!fread(str, 1, 8, anyin)) {           /* 8 */
		   return;
	   }
	   str[8] = '\0';
	   if (strcmp(str, "WAVEfmt ") != 0) nowav = TRUE;
	   /* Expect length of fmt data, which should be 16 */
	   if (!fread(&tempuint, 4, 1, anyin)) {	/* 16 */
		   return;
	   }
	   if (tempuint != 16) nowav = TRUE;
	   /* Expect format tag, which should be 1 for pcm */
	   if (!fread(&tempushort, 2, 1, anyin)) { /* 20 */
		   return;
	   }
	   if (tempushort != 1)
		   nowav = TRUE;
	   /* Expect number of channels */
	   if (!fread(&cn, 2, 1, anyin)) { /* 20 */
		   return;
	   }
	   if (cn != 1 && cn != 2) nowav = TRUE;
	   /* Read samplefrequency */
	   if (!fread(&sf, 4, 1, anyin)) {  /* 24 */
		   return;
	   }
	   /* Read bytes per second: Should be samplefreq * channels * 2 */
	   if (!fread(&tempuint, 4, 1, anyin)) {         /* 28 */
		   return;
	   }
	   if (tempuint != sf * cn * 2) nowav = TRUE;
	   /* read bytes per frame: Should be channels * 2 */
	   if (!fread(&tempushort, 2, 1, anyin)) {       /* 32 */
		   return;
	   }
	   if (tempushort != cn * 2) nowav = TRUE;
	   /* Read bits per sample: Should be 16 */
	   if (!fread(&tempushort, 2, 1, anyin)) {       /* 34 */
		   return;
	   }
	   if (tempushort != 16) nowav = TRUE;
	   if (!fread(str, 4, 1, anyin)) {            /* 36 */
		   return;
	   }
	   str[4] = '\0';
	   if (strcmp(str, "data") != 0) nowav = TRUE;
	   if (!fread(&tempuint, 4, 1, anyin)) {   /* 40 */
		   return;
	   }
	   if (nowav)
	   {
		   fseek(anyin, 0, SEEK_SET);   /* Back to beginning of file */
		   chat("File has no WAV header.\n");
	   }
	   else
	   {
		   samplefrequency = sf;
		   channels = cn;
		   chat("Read WAV header: %d channels, samplefrequency %d.\n",
			 channels, samplefrequency);
		   iswav = TRUE;
	   }
   }
   return;
}



/* -----------------------------------------------------------------------
   Write a .WAV header to 'out'. See header for details.
   -----------------------------------------------------------------------*/
void makewavheader( void)
{
   unsigned int tempuint, filelength;
   unsigned short tempushort;

   /* If fseek fails, don't create the header. */
   if (fseek(out, 0, SEEK_END) != -1)
   {
	   filelength = ftell(out);
	   chat("filelength %d, ", filelength);
	   fseek(out, 0, SEEK_SET);
	   if (!fwrite("RIFF", 1, 4, out)) { /* 0 */
		   return;
	   }
	   tempuint = filelength - 8;
	   if (!fwrite(&tempuint, 4, 1, out)) {    /* 4 */
		   return;
	   }
	   if (!fwrite("WAVEfmt ", 1, 8, out)) {   /* 8 */
		   return;
	   }
	   /* length of fmt data 16 bytes */
	   tempuint = 16;
	   if (!fwrite(&tempuint, 4, 1, out)) {   /* 16 */
		   return;
	   }
	   /* Format tag: 1 for pcm */
	   tempushort = 1;
	   if (!fwrite(&tempushort, 2, 1, out)) { /* 20 */
		   return;
	   }
	   chat("%d channels\n", channels);
	   if (!fwrite(&channels, 2, 1, out)) {
		   return;
	   }
	   chat("samplefrequency %d\n", samplefrequency);
	   if (!fwrite(&samplefrequency, 4, 1, out)) {   /* 24 */
		   return;
	   }
	   /* Bytes per second */
	   tempuint = channels * samplefrequency * 2;
	   if (!fwrite(&tempuint, 4, 1, out)) {         /* 28 */
		   return;
	   }
	   /* Block align */
	   tempushort = 2 * channels;
	   if (!fwrite(&tempushort, 2, 1, out)) {       /* 32 */
		   return;
	   }
	   /* Bits per sample */
	   tempushort = 16;
	   if (!fwrite(&tempushort, 2, 1, out)) {       /* 34 */
		   return;
	   }
	   if (!fwrite("data", 4, 1, out)) {            /* 36 */
		   return;
	   }
	   tempuint = filelength - 44;
	   if (!fwrite(&tempuint, 4, 1, out)) {   /* 40 */
		   return;
	   }
   }
   return;
}

/* -----------------------------------------------------------------------
   After all is read and done, inform the inclined user of the elapsed time
   -----------------------------------------------------------------------*/
static void statistics( void)
{
   int temp;

   temp = time(NULL) - stopwatch;
   if (temp != 1)
   {
      inform ("\nTime: %d seconds\n", temp);
   }
   else
   {
      inform ("\nTime: 1 second\n");
   }
   return;
}


/* -----------------------------------------------------------------------
   Start the stopwatch and make sure the user is informed at end of program.
   -----------------------------------------------------------------------*/
void startstopwatch(void)
{
   stopwatch = time(NULL);       /* Remember time 'now' */
   atexit(statistics);           /* Call function statistics() at exit. */

   return;
}

/* --------------------------------------------------------------------
   Tests the character 'coal' for being a command line option character,
   momentarrily '-'.
   -------------------------------------------------------------------- */
int isoptionchar (char coal)
{
   return (coal =='-');
}

/* -----------------------------------------------------------------------
   Reads through the arguments on the lookout for an option starting
   with 'string'. The rest of the option is read as a time and passed
   to *result, where the result is meant to mean 'number of samples' in
   that time.
   On failure, *result is unchanged.
   return value is TRUE on success, FALSE otherwise.
   -----------------------------------------------------------------------*/
int parsetimearg( int argcount, char *args[], char *string, int *result)
{
    int i;

    if ((i = findoption( argcount, args, string)) > 0)
    {
	if (parsetime(args[i] + 1 + strlen( string), result))
	    return TRUE;
	argerrornum(args[i]+1, ME_NOTIME);
    }
    return FALSE;
}

/* -----------------------------------------------------------------------
   The string argument is read as a time and passed
   to *result, where the result is meant to mean 'number of samples' in
   that time.
   On failure, *result is unchanged.
   return value is TRUE on success, FALSE otherwise.
   -----------------------------------------------------------------------*/
int parsetime(char *string, int *result)
{
    int k;
    double temp;
    char m, s, end;

    k = sscanf(string, "%30lf%1c%1c%1c", &temp, &m, &s, &end);
    switch (k)
      {
      case 0: case EOF: case 4:
	return FALSE;
      case 1:
	*result = temp;
	break;
      case 2:
	if (m == 's')
	  *result = temp * samplefrequency;
	else
	  return FALSE;
	break;
      case 3:
	if (m == 'm' && s == 's')
	  *result = temp * samplefrequency / 1000;
	else if (m == 'H' && s == 'z')
	  *result = samplefrequency / temp;
	else
	  return FALSE;
	break;
      default:
	argerrornum(NULL, ME_THISCANTHAPPEN);
      }
    return TRUE;
}

/* -----------------------------------------------------------------------
   The string argument is read as a frequency and passed
   to *result, where the result is meant to mean 'number of samples' in
   one cycle of that frequency.
   On failure, *result is unchanged.
   return value is TRUE on success, FALSE otherwise.
   -----------------------------------------------------------------------*/
int parsefreq(char *string, double *result)
{
    int k;
    double temp;
    char m, s, end;

    k = sscanf(string, "%30lf%1c%1c%1c", &temp, &m, &s, &end);
    switch (k)
      {
      case 0: case EOF: case 2: case 4:
	return FALSE;
      case 1:
	*result = temp;
	break;
      case 3:
	if (m == 'H' && s == 'z')
	  *result = samplefrequency / temp;
	else
	  return FALSE;
	break;
      default:
	argerrornum(NULL, ME_THISCANTHAPPEN);
      }
    return TRUE;
}

char *parsefilearg( int argcount, char *args[])
{
  int i;
  char *result = NULL;

   for (i = 1; i < argcount; i++)
   {
      if (args[i][0] != '\0' &&
	  (!isoptionchar (args[i][0]) || args[i][1] == '\0' ))
      {
	/*---------------------------------------------*
	 * The argument is a filename:                 *
	 * it is either no dash followed by something, *
	 * or it is a dash following by nothing.       *
	 *---------------------------------------------*/
	result = malloc( strlen( args[i]) + 1);
	if (result == NULL)
	    fatalperror( "Couldn't allocate memory for filename\n");
	strcpy( result, args[i]);
	args[i][0] = '\0';                    /* Mark as used up */
	break;
      }
   }
   return result;
}

int parseswitch( char *found, char *wanted)
{
  if (strncmp( found, wanted, strlen( wanted)) == 0)
    {
      if (found[strlen( wanted)] == '\0')
	return TRUE;
      else
	argerrornum( found, ME_NOSWITCH);
    }
  return FALSE;
}

int parseswitcharg( int argcount, char *args[], char *string)
{
  int i;

  if ((i = findoption( argcount, args, string)) > 0)
    {
      if (args[i][strlen( string) + 1] == '\0')
	return TRUE;
      else
	argerrornum( args[i] + 1, ME_NOSWITCH);
    }
  return FALSE;
}

int parseintarg( int argcount, char *args[], char *string, int *result)
{
  int i, temp;
  char c;

  if ((i = findoption( argcount, args, string)) > 0)
   {
      switch (sscanf(args[i] + 1 + strlen( string),
		     "%30d%1c", &temp, &c))
      {
	case 0: case EOF: case 2:
            argerrornum(args[i]+1, ME_NOINT);
            return FALSE;
         case 1:
	   *result = temp;
            break;
         default:
            say("frame.c: This can't happen\n");
      }
      return TRUE;
   }
  else
    {
      return FALSE;
    }
}

/* --------------------------------------------------------------------
   Reads through the arguments on the lookout for an option starting
   with 'string'. The rest of the option is read as a double and
   passed to *result.
   On failure, *result is unchanged.
   return value is TRUE on success, FALSE otherwise.
   -------------------------------------------------------------------- */
int parsedoublearg( int argcount, char *args[], char *string, double *result)
{
  int i;
  double temp;
  char end;

  if ((i = findoption( argcount, args, string)) > 0)
    {
      switch (sscanf(args[i] + 1 + strlen( string), "%30lf%1c", &temp, &end))
	{
	case 0: case EOF: case 2:
	  argerrornum(args[i]+1, ME_NODOUBLE);
	  return FALSE;
	case 1:
	  *result = temp;
	  break;
	default:
	  say("frame.c: This can't happen\n");
	}
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

/* --------------------------------------------------------------------
   Reads through the arguments on the lookout for an option starting
   with 'string'. The rest of the option is read as a volume, i.e.
   absolute, percent or db. The result is passed to *result.
   On failure, *result is unchanged.
   return value is TRUE on success, FALSE otherwise.
   -------------------------------------------------------------------- */
int parsevolarg( int argcount, char *args[], char *string, double *result)
{
  double vol = 1.0;
  char sbd, sbb, end;
  int i, weird = FALSE;

  if ((i = findoption( argcount, args, string)) > 0)
    {
      switch (sscanf(args[i] + 1 + strlen( string),
		     "%30lf%1c%1c%1c", &vol, &sbd, &sbb, &end))
	{
	  case 0: case EOF: case 4:
	  weird = TRUE;
	  break;    /* No number: error */
	case 1:
	  *result = vol;
	  break;
	case 2:
	  if (sbd == '%')
	    *result = vol / 100;
	  else
	    weird = TRUE;    /* One char but no percent: error */
	  break;
	case 3:
	  if (sbd =='d' && sbb == 'b')
	    *result = pow(2, vol / 6.02);
	  else
	    weird = TRUE;    /* Two chars but not db: error */
	  break;
	default:
	  say("frame.c: This can't happen.\n");
	}
      if (weird)
	argerrornum( args[i] + 1, ME_NOVOL);
	  /* ("Weird option: couldn't parse volume '%s'\n", args[i]+2); */
      return !weird;
    }
  else
    {
      return FALSE;
    }
}


/* --------------------------------------------------------------------
   Reads the specified string 's' and interprets it as a volume. The string
   would be of the form 1.8 or 180% or 5db.
   On success, the return value TRUE and *result is given result
   (i.e. the relative volume, i.e. 1.8). On failure, FALSE is returned and
   result is given value 1.0.
   -------------------------------------------------------------------- */
int parsevolume(char *s, double *result)
{
    int k;
    char sbd, sbb, end;

    *result = 1.0;
    k = sscanf(s, "%30lf%1c%1c%1c", result, &sbd, &sbb, &end);
    switch (k)
    {
      case 0:
      case EOF:
      case 4:
       return FALSE;
      case 1:
       break;
      case 2:
       if (sbd != '%')
	   return FALSE;
       (*result) /=100;
       break;
      case 3:
       if (sbd !='d' || sbb != 'b')
	   return FALSE;
       (*result) = pow(2, (*result) / 6.02);
       break;
      default:
       say("parsevolume: This can't happen (%d).\n", k);
    }
    return TRUE;
}

/* --------------------------------------------------------------------
   Reports an error due to parsing the string 's' encountered on the
   command line.
   -------------------------------------------------------------------- */
void argerror(char *s)
{
  error ("Error parsing command line. Unrecognized option:\n\t-%s\n", s);
  fatalerror("\nTry --help for help.\n");
}

/* --------------------------------------------------------------------
   Reports an error due to parsing the string 's' encountered on the
   command line. 'code' indicates the type of error.
   -------------------------------------------------------------------- */
void argerrornum(char *s, Errornum code)
{
  char *message;

  if (code == ME_TOOMANYFILES)
    {
      error("Too many files on command line: '%s'.\n", s);
    }
  else
    {
      if (s != NULL)
	error ("Error parsing option -%s:\n\t", s);
      switch( code)
	{
	case ME_NOINT:
	  message = "Integer expected";
	  break;
	case ME_NODOUBLE:
	  message = "Floating point number expected";
	  break;
	case ME_NOTIME:
	  message = "Time argument expected";
	  break;
	case ME_NOVOL:
	  message = "Volume argument expected";
	  break;
	case ME_NOSWITCH:
	  message = "Garbage after switch-type option";
	  break;
	case ME_HEADERONTEXTFILE:
	  message = "Option -h is not useful for text-output";
	  break;
	case ME_NOINFILE:
	  message = "No input file specified";
	  break;
	case ME_NOOUTFILE:
	  message = "No output file specified";
	  break;
	case ME_NOIOFILE:
	  message = "No input/output file specified";
	  break;
	case ME_NOSTDIN:
	  message = "Standard in not supported here";
	  break;
	case ME_NOSTDOUT:
	  message = "Standard out not supported here";
	  break;
	case ME_NOSTDIO:
	  message = "Standard in/out not supported here";
	  break;
	case ME_NOTENOUGHFILES:
	  message = "Not enough files specified";
	  break;
	case ME_THISCANTHAPPEN:
	  fatalerror("\nThis can't happen. Report this as a bug\n");
	  /* fatalerror does not return */
	default:
	  error("Error code %d not implemented. Fix me!\n", code);
	  message = "Error message not implemented. Fix me!";
	}
      error("%s\n", message);
    }
  fatalerror("\nTry --help for help.\n");
}

/* --------------------------------------------------------------------
   Reports an error due to parsing the string 's' encountered on the
   command line. 'message' explains the type of error.
   -------------------------------------------------------------------- */
void argerrortxt(char *s, char *message)
{
  if (s != NULL)
    error ("Error parsing option -%s:\n\t", s);
  else
    error ("Error parsing command line:\n\t");
  error ("%s\n", message);
  fatalerror("\nTry --help for help.\n");
}

/* --------------------------------------------------------------------
   Check for any remaining arguments and complain about their existence
   -------------------------------------------------------------------- */
void checknoargs( int argcount, char *args[])
{
  int i, errorcount = 0;

  for (i = 1; i < argcount; i++)
    {
      if (args[i][0] != '\0')   /* An unused argument! */
	{
	  errorcount++;
	  if (errorcount == 1)
	    error("The following arguments were not recognized:\n");
	  error("\t%s\n", args[i]);
	}
    }
  if (errorcount > 0)           /* Errors are fatal */
    fatalerror("\nTry --help for help.\n");

  return;                       /* No errors? Return. */
}

/* --------------------------------------------------------------------
   Parses the command line arguments as represented by the function
   arguments. Sets the global variables 'in', 'out', 'samplefrequency'
   and 'samplewidth' accordingly. Also verboselevel.
   The files 'in' and 'out' are even opened according to 'fileswitch'.
   See headerfile for details
   -------------------------------------------------------------------- */
void parseargs( int argcount, char *args[], int fileswitch)
{
   char *filename;
   int tempint = 0;

   if ((fileswitch & 1) != 0)     /* If getting infile  */
     in = NULL;
   if ((fileswitch & 4) != 0)     /* If getting outfile */
     out = NULL;
   wavout = FALSE;
   verboselevel = 5;
   samplefrequency = DEFAULTFREQ;
   samplewidth = 2;
   channels = 1;

   /*-----------------------------------------------*
    * First first check testcase, usage and version *
    *-----------------------------------------------*/
   test_usage = parseswitcharg( argcount, args, "-test-usage");
   if (parseswitcharg( argcount, args, "-help"))
       {
	 printf("%s%s", usage, standardusage);
	 exit(0);
       }
   if (parseswitcharg( argcount, args, "-version"))
       {
	 printf("%s\n(%s)\n", version, standardversion);
	 exit(0);
       }
   /*--------------------------------------*
    * Set verboselevel                     *
    *--------------------------------------*/
   while (parseswitcharg( argcount, args, "V"))
               verboselevel = 10;
   while (parseswitcharg( argcount, args, "Q"))
               verboselevel = 1;
   /*-------------------------------------------------*
    * Get filenames and open files *
    *-------------------------------------------------*/
   if ((fileswitch & 1) != 0)        /* Infile wanted */
     {
       infilename = parsefilearg( argcount, args);
       if (infilename == NULL)
	 argerrornum( NULL, ME_NOINFILE);
       if (strcmp( infilename, "-") == 0)
	 {
	   infilename = "<stdin>";
	   in = stdin;
	   if ((fileswitch & 2) != 0)   /* Binfile wanted */
	     readwavheader( in);
	 }
       else
	 {
	   if ((fileswitch & 2) == 0)   /* Textfile wanted */
	     in = fopen(infilename, "rt");
	   else                         /* Binfile wanted */
	     if ((in = fopen(infilename, "rb")) != NULL)
	       readwavheader( in);
	 }
       if (in == NULL)
	 fatalerror("Error opening input file '%s': %s\n", infilename,strerror(errno));
       else
	 inform("Using file '%s' as input\n", infilename);
     }
   if ((fileswitch & 4) != 0)        /* Outfile wanted */
     {
       outfilename = parsefilearg( argcount, args);
       if (outfilename == NULL)
	 argerrornum( NULL, ME_NOOUTFILE);
       if (strcmp( outfilename, "-") == 0)
	 {
	   outfilename = "<stdout>";
	   out = stdout;
	 }
       else
	 {

	   if ((fileswitch & 8) == 0)   /* Textfile wanted */
	     out = fopen(outfilename, "wt");
	   else                         /* Binfile wanted */
	     out = fopen(outfilename, "wb");
	 }
       if (out == NULL)
	 fatalerror("Error opening output file '%s': %s\n", outfilename,strerror(errno));
       else
	 inform("Using file '%s' as output\n", outfilename);
     }
   if ((fileswitch & 32) != 0)      /* In-/Outfile wanted */
     {
       assert (in == NULL && out == NULL);
       infilename = outfilename = parsefilearg( argcount, args);
       if (outfilename == NULL)
	 argerrornum( NULL, ME_NOIOFILE);
       if (strcmp( infilename, "-") == 0)
	 argerrornum( infilename, ME_NOSTDIN);
       inform("Using file '%s' as input/output\n", outfilename);
       in = out = fopen(outfilename, "r+");
       if (out == NULL)
	 fatalerror("Error opening input/output file '%s': %s\n", outfilename,strerror(errno));

       readwavheader( in);
     }
   if ((fileswitch & 16) == 0)  /* No additional files wanted */
     {
       if ((filename = parsefilearg( argcount, args)) != NULL)
	 argerrornum( filename, ME_TOOMANYFILES);
     }

   /*-------------------------------------------------*
    * Set samplefrequency, width, wavout, 
    *-------------------------------------------------*/
   parseintarg( argcount, args, "f", &samplefrequency);
   wavout = parseswitcharg( argcount, args, "h");
   if (parseintarg( argcount, args, "w", &tempint))
     {
       if (tempint != 16)
	 argerrortxt(NULL, "Option -w is only valid "
		     "with value 16. Sorry.");
       else
	 samplewidth = tempint;
     }
   if (parseintarg( argcount, args, "c", &tempint))
     {
       if (tempint != 1 && tempint != 2)
	 argerrortxt(NULL, "Option -c is only valid "
		     "with values 1 or 2. Sorry.");
       else
	 channels = tempint;
     }
   /*-------------------------------------------------*
    * Create WAV-header on output if wanted.          *
    *-------------------------------------------------*/
   if (wavout)
     switch (fileswitch & (12))
       {
       case 4:   /* User wants header on textfile */
	 argerrornum( NULL, ME_HEADERONTEXTFILE);
       case 12:  /* User wants header on binfile  */
	 makewavheader();
	 break;
       case 0:   /* User wants header, but there is no outfile */
	 /* Problem: what about i/o-file, 32? You might want a header
	    on that? Better ignore this case. */
	 break;
       case 8:    /* An application musn't ask for this */
       default:   /* This can't happen */
	 assert( FALSE);
       }
   return;
}

/* --------------------------------------------------------------------
   Returns the index 'i' of the first argument that IS an option, and
   which begins with the label 's'. If there is none, -1.
   We also mark that option as done with, i.e. we cross it out.
   -------------------------------------------------------------------- */
int findoption( int argcount, char *args[], char *s)
{
   int i;

   if (test_usage)
     printf("Checking for option -%s\n", s);

   for (i=1; i<argcount; i++)
   {
     if (isoptionchar (args[i][0]) &&
	 strncmp( args[i] + 1, s, strlen( s)) == 0)
       {
	 args[i][0] = '\0';
	 return i;
       }
   }
   return -1;
}

/* --------------------------------------------------------------------
   Finishes off the .WAV header (if any) and exits correctly and formerly.
   -------------------------------------------------------------------- */
int myexit (int value)
{
  switch (value)
    {
    case 0:
      if (wavout)
	makewavheader();  /* Writes a fully informed .WAV header */
      chat("Success!\n");
      break;
    default:
      chat("Failure.\n");
      break;
    }
  exit (value);
}

/* --------------------------------------------------------------------
   Reads the stated input file bufferwise, calls the function 'work'
   with the proper values, and writes the result to the stated output file.
   Return value: TRUE on success, FALSE otherwise.
   -------------------------------------------------------------------- */
int workloop( FILE *theinfile, FILE *theoutfile,
	      int (*work)( short *buffer, int length) )
{
  short *buffer;
  int length, nowlength;

  length = BUFFSIZE;
  if ((buffer = malloc( sizeof(short) * length)) == NULL)
    fatalperror ("");
  while (TRUE)
    {
      nowlength = fread(buffer, sizeof(short), length, theinfile);
      if (ferror( theinfile) != 0)
	fatalperror("Error reading input file");
      if (nowlength == 0)   /* Reached end of input file */
	break;
      /* Call the routine that does the work */
      if (!work (buffer, nowlength))         /* On error, stop. */
	return FALSE;
      if (!fwrite(buffer, sizeof(short), nowlength, theoutfile)) {
	      return FALSE;
      }
      if (ferror( theoutfile) != 0)
	fatalperror("Error writing to output file");
    }
  return TRUE;      /* Input file done with, no errors. */
}

int __attribute__((format(printf, 1, 2))) chat( const char *format, ...)
{
    va_list ap;
    int result = 0;

    if (verboselevel > 5)
    {
	va_start( ap, format);
	result = vfprintf( stderr, format, ap);
	va_end( ap);
    }
    return result;
}


int __attribute__((format(printf, 1, 2))) inform( const char *format, ...)
{
    va_list ap;
    int result = 0;

    if (verboselevel > 1)
    {
	va_start( ap, format);
	result = vfprintf( stderr, format, ap);
	va_end( ap);
    }
    return result;
}

int __attribute__((format(printf, 1, 2))) error( const char *format, ...)
{
    va_list ap;
    int result;

    va_start( ap, format);
    result = vfprintf( stderr, format, ap);
    va_end( ap);
    return result;
}

void __attribute__((format(printf, 1, 2))) fatalerror( const char *format, ...)
{
    va_list ap;

    va_start( ap, format);
    vfprintf( stderr, format, ap);
    va_end( ap);
    myexit(1);
}

void fatalperror( const char *string)
{
  perror( string);
  myexit( 1);
}

int __attribute__((format(printf, 1, 2))) say( const char *format, ...)
{
    va_list ap;
    int result;

    va_start( ap, format);
    result = vfprintf( stdout, format, ap);
    va_end( ap);
    return result;
}


char *malloccopy( char *string)
{
    char *result;

    result = malloc( strlen( string) + 1);
    if (result != NULL)
	strcpy( result, string);
    return result;
}


char *mallocconcat( char *one, char *two)
{
    char *result;

    result = malloc( strlen( one) + strlen( two) + 1);
    if (result != NULL)
      {
	strcpy( result, one);
	strcat( result, two);
      }
    return result;
}

double double2db( double value)
{
  if (value < 0)
    value = -value;
  return 6.0 * log( value / 32767) / log( 2);
}

void readawaysamples( FILE *in, size_t size)
{
  short *buffer;
  int samplesread, count;

  buffer = malloc( sizeof( *buffer) * BUFFSIZE);
  if (buffer == NULL) fatalperror("Couldn't allocate buffer");

  while (size > 0)
    {
      if (size > BUFFSIZE)
	count = BUFFSIZE;
      else
	count = size;

      samplesread = fread( buffer, sizeof(*buffer), count, in);
      if (ferror( in) != 0)
	fatalperror("Error reading input file");
      size -= samplesread;
    }
  free( buffer);
}

