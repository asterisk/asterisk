/****************************************************************************
 *
 * Programs for processing sound files in raw- or WAV-format.
 * -- Useful functions for parsing command line options and
 *    issuing errors, warnings, and chit chat.
 *
 * Name:    frame.h
 * Version: see frame.c
 * Author:  Mark Roberts <mark@manumark.de>
 *
 ****************************************************************************/
/****************************************************************************
 *  These are useful functions that all DSP programs might find handy
 ****************************************************************************/

/* fileswitch for parseargs:

   The following are masks for several different ways of opening files.
   --------------------------------------------------------------------
   Bit 0: Open infile?
   Bit 1: Open infile as binary (as opposed to text)
   Bit 2: Open outfile?
   Bit 3: Open outfile as binary (as opposed to text)
   Bit 4: Do not complain about too many file arguments
   Bit 5: Open one file for input AND output, binary.
*/
#define INTEXT (1+0)
#define INBIN (1+2)
#define OUTTEXT (4)
#define OUTBIN (4+8)
#define NOFILES (0)
#define NOCOMPLAIN (16)
#define IOBIN (32)

#ifndef FALSE
 #define FALSE (0==1)
 #define TRUE (0==0)
#endif

extern int samplefrequency;
extern unsigned short samplewidth;
extern unsigned short channels;
extern int wavout;         /* TRUE iff out file is .WAV file */
extern int iswav;          /* TRUE iff in file was found to be a .WAV file */
extern FILE *in, *out;
extern char *infilename, *outfilename;
extern int verboselevel;
extern char *version;      /* String to be issued as version string. Should
			      be set by application. */
extern char *usage;        /* String to be issued as usage string. Should be
			      set by application. */

#define DEFAULTFREQ 44100
#define BUFFSIZE 50000   /* How many samples to read in one go (preferred) */
#define MINBUFFSIZE 5000 /* How many samples to read in one go (minimum)   */

/*************************************************
 * Types of errors handled by argerrornum()      *
 *************************************************/
typedef enum
{
  ME_NOINT,
  ME_NODOUBLE,
  ME_NOTIME,
  ME_NOVOL,
  ME_NOSWITCH,
  ME_TOOMANYFILES,
  ME_HEADERONTEXTFILE,
  ME_NOINFILE,
  ME_NOOUTFILE,
  ME_NOIOFILE,
  ME_NOSTDIN,
  ME_NOSTDOUT,
  ME_NOSTDIO,
  ME_NOTENOUGHFILES,
  ME_THISCANTHAPPEN
} Errornum;


/* -----------------------------------------------------------------------
   Create memory and copy 'string', returning a pointer to the copy.
   NULL is returned if malloc fails.
   -----------------------------------------------------------------------*/
extern char *malloccopy( char *string);

/* -----------------------------------------------------------------------
   Start the stopwatch and make sure the user is informed at end of program.
   -----------------------------------------------------------------------*/
extern void startstopwatch(void);

/* -----------------------------------------------------------------------
   Writes the number of samples to result that are yet to be read from anyin.
   I.e. the number of remaining bytes is divided by the number of bytes per
   sample value, but not by the number of channels.
   Return values are TRUE on success, FALSE on failure.
   -----------------------------------------------------------------------*/
extern int getremainingfilelength( FILE *anyin, long *result);

/* -----------------------------------------------------------------------
   Read a .pk-header from 'anyin' and printf the entries.
   -----------------------------------------------------------------------*/
void readpkheader( FILE *anyin);

/* -----------------------------------------------------------------------
   Read a .WAV header from 'anyin'.
   If it is recognised, the data is used.
   Otherwise, we assume it's PCM-data and ignore the header.
   The global variable 'iswav' is set on success, otherwise cleared.
   -----------------------------------------------------------------------*/
extern void readwavheader( FILE *anyin);

/* -----------------------------------------------------------------------
   Write a .WAV header to 'out'.
   The filepointer is placed at the end of 'out' before operation.
   This should be called before any data is
   written, and again, when ALL the data has been written.
   First time, this positions the file pointer correctly; second time, the
   missing data can be inserted that wasn't known the first time round.
   -----------------------------------------------------------------------*/
extern void makewavheader( void);

/* --------------------------------------------------------------------
   Tests the character 'coal' for being a command line option character,
   momentarily '/' or '-'.
   -------------------------------------------------------------------- */
extern int isoptionchar (char coal);

/* -----------------------------------------------------------------------
   Reads through the arguments on the lookout for an option starting
   with 'string'. The rest of the option is read as a time and passed
   to *result, where the result is meant to mean 'number of samples' in
   that time.
   On failure, *result is unchanged.
   return value is TRUE on success, FALSE otherwise.
   -----------------------------------------------------------------------*/
extern int parsetimearg( int argcount, char *args[], char *string,
			 int *result);

/* -----------------------------------------------------------------------
   The string argument is read as a time and passed to *result, where
   the result is meant to mean 'number of samples' in that time.  On
   failure, *result is unchanged.
   return value is TRUE on success, FALSE otherwise.
   -----------------------------------------------------------------------*/
int parsetime(char *string, int *result);

/* -----------------------------------------------------------------------
   The string argument is read as a frequency and passed
   to *result, where the result is meant to mean 'number of samples' in
   one cycle of that frequency.
   On failure, *result is unchanged.
   return value is TRUE on success, FALSE otherwise.
   -----------------------------------------------------------------------*/
int parsefreq(char *string, double *result);

/* --------------------------------------------------------------------
   Reads through the arguments on the lookout for a switch -'string'.
   return value is TRUE if one exists, FALSE otherwise.
   If characters remain after the switch, a fatal error is issued.
   -------------------------------------------------------------------- */
extern int parseswitcharg( int argcount, char *args[], char *string);

/* --------------------------------------------------------------------
   Reads through the arguments on the lookout for an option starting
   with 'string'. The rest of the option is read as an integer and
   passed to &result.
   On failure, &result is unchanged.
   return value is TRUE on success, FALSE otherwise.
   -------------------------------------------------------------------- */
extern int parseintarg( int argcount, char *args[], char *string,
			 int *result);

/* --------------------------------------------------------------------
   Reads through the arguments on the lookout for a filename, i.e. anything
   that does not start with the optionchar. The filename is copied to
   newly allocated memory, a pointer to which is returned.
   The argument is marked as used. Therefore repeated use of this function
   will yield a complete list of filenames on the command-line.
   If malloc() fails, the function does not return.
   -------------------------------------------------------------------- */
extern char *parsefilearg( int argcount, char *args[]);

/* --------------------------------------------------------------------
   Reads through the arguments on the lookout for an option starting
   with 'string'. The rest of the option is read as a double and
   passed to *result.
   On failure, *result is unchanged.
   return value is TRUE on success, FALSE otherwise.
   -------------------------------------------------------------------- */
extern int parsedoublearg( int argcount, char *args[], char *string,
			   double *result);

/* --------------------------------------------------------------------
   Reads through the arguments on the lookout for an option starting
   with 'string'. The rest of the option is read as a volume, i.e.
   absolute, percent or db. The result is passed to *result.
   On failure, *result is unchanged.
   -------------------------------------------------------------------- */
extern int parsevolarg( int argcount, char *args[], char *string,
			 double *result);

/* --------------------------------------------------------------------
   Reads the specified string and interprets it as a volume. The string
   would be of the form 1.8 or 180% or 5db.
   On success, the return value is the relative volume, i.e. 1.8
   On failure, -1 is returned.
   -------------------------------------------------------------------- */
extern int parsevolume(char *s, double *result);

/* --------------------------------------------------------------------
   Reads through the arguments on the lookout for a switch -'string'.
   return value is TRUE if one exists, FALSE otherwise.
   If characters remain after the switch, a fatal error is issued.
   -------------------------------------------------------------------- */
extern int parseswitch( char *found, char *wanted);

/* --------------------------------------------------------------------
   Reports an error due to parsing the string 's' encountered on the
   command line.
   -------------------------------------------------------------------- */
extern void argerror(char *s);

/* --------------------------------------------------------------------
   Reports an error due to parsing the string 's' encountered on the
   command line. 'code' indicates the type of error.
   -------------------------------------------------------------------- */
extern void argerrornum(char *s, Errornum code);

/* --------------------------------------------------------------------
   Reports an error due to parsing the string 's' encountered on the
   command line. 'message' explains the type of error.
   -------------------------------------------------------------------- */
extern void argerrortxt(char *s, char *message);

/* --------------------------------------------------------------------
   Check for any remaining arguments and complain about their existence.
   If arguments are found, this function does not return.
   -------------------------------------------------------------------- */
extern void checknoargs( int argcount, char *args[]);

/* --------------------------------------------------------------------
   Parses the command line arguments as represented by the function
   arguments. Sets the global variables 'in', 'out', 'samplefrequency'
   and 'samplewidth' accordingly.
   According to 'fileswitch', in and out files are opened or not. See
   above for an explanation of 'fileswitch'.
   -------------------------------------------------------------------- */
extern void parseargs( int argcount, char *args[], int fileswitch);

/* --------------------------------------------------------------------
   Returns the index 'i' of the first argument that IS an option, and
   which begins with the label 's'. If there is none, -1.
   We also mark that option as done with, i.e. we cross it out.
   -------------------------------------------------------------------- */
extern int findoption( int argcount, char *args[], char *s);

/* --------------------------------------------------------------------
   Finishes off the .WAV header (if any) and exits correctly and formerly.
   -------------------------------------------------------------------- */
extern int myexit (int value);

/* --------------------------------------------------------------------
   Reads the stated input file bufferwise, calls the function 'work'
   with the proper values, and writes the result to the stated output file.
   Return value: TRUE on success, FALSE otherwise.
   -------------------------------------------------------------------- */
extern int workloop( FILE *theinfile, FILE *theoutfile,
		     int (*work)( short *buffer, int length) );

/* --------------------------------------------------------------------
   Five functions for printing to stderr. Depending on the level of verbose,
   output may be supressed. fatalerror() is like error() but does not return.
   fatalperror() is like the standard function perror() but does not return.
   -------------------------------------------------------------------- */
extern int chat( const char *format, ...);
extern int inform( const char *format, ...);
extern int error( const char *format, ...);
extern void fatalerror( const char *format, ...);
extern void fatalperror( const char *string);

/* --------------------------------------------------------------------
   And one functions for printing to stdout.
   -------------------------------------------------------------------- */
extern int say( const char *format, ...);

/* --------------------------------------------------------------------
   Allocate memory for it and return a pointer to a string made up of
   the two argument strings.
   -------------------------------------------------------------------- */
extern char *mallocconcat( char *one, char *two);

/* --------------------------------------------------------------------
   Convert a sample value to decibel.
   -------------------------------------------------------------------- */
extern double double2db( double value);

/* --------------------------------------------------------------------
   Read 'size' samples from file 'in' and lose them.
   -------------------------------------------------------------------- */
extern void readawaysamples( FILE *in, size_t size);
