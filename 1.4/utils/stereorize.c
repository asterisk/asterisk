/****************************************************************************
 *
 * Programs for processing sound files in raw- or WAV-format.
 * -- Merge two mono WAV-files to one stereo WAV-file.
 *
 * Name:    stereorize.c
 * Version: 1.1
 * Author:  Mark Roberts <mark@manumark.de>
 *	    Michael Labuschke <michael@labuschke.de>
 *
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include "frame.h"

static char *Version = "stereorize 1.1, November 5th 2000";
static char *Usage =
"Usage: stereorize [options] infile-left infile-right outfile\n\n"

"Example:\n"
" stereorize left.wav right.wav stereo.wav -h\n\n"

"Creates stereo.wav (with WAV-header, option -h) from data in mono files\n"
"left.wav and right.wav.\n"
;

int main( int argcount, char *args[])
{
   int i, k[2], maxk, stdin_in_use=FALSE;
   short *leftsample, *rightsample, *stereosample;
   FILE *channel[2];
   char *filename[2], *tempname;

   version = Version;
   usage = Usage;

   channel[0] = NULL;
   channel[1] = NULL;

   parseargs( argcount, args, NOFILES | NOCOMPLAIN);

   for (i = 0; i < 2; i++)
     {
       filename[i] = parsefilearg( argcount, args);
       if (filename[i] == NULL)
	 argerrornum( NULL, ME_NOTENOUGHFILES);
       if (strcmp (filename[i], "-") == 0)
	 {
	   if (stdin_in_use)
	     argerrortxt( filename[i] + 1,
			  "Cannot use <stdin> for both input files");
	   filename[i] = "<stdin>";
	   channel[i] = stdin;
	   stdin_in_use = TRUE;
	 }
       else
	 {
	   channel[i] = fopen(filename[i], "rb");
	 }
       if (channel[i] == NULL)
	   fatalerror( "Error opening input file '%s': %s\n", filename[i],strerror(errno));
       else
	 inform("Using file '%s' as input\n", filename[i]);
     }
   for (i = 0; i < 2; i++)
     {
       assert ( channel[i] != NULL);
       readwavheader( channel[i]);
       if (iswav && channels != 1)
	 inform("Warning: '%s' is no mono file\n", filename[i]);
     }

   outfilename = parsefilearg( argcount, args);
   if (outfilename == NULL) argerrornum( NULL, ME_NOOUTFILE);
   if (strcmp (outfilename, "-") == 0)
     {
       outfilename = "<stdout>";
       out = stdout;
     }
   else
     {
       out = fopen(outfilename, "wb");
     }
   if (out == NULL)
     fatalerror( "Error opening output file '%s': %s\n", outfilename,strerror(errno));
   else
     inform("Using file '%s' as output\n", outfilename);

   if ((tempname = parsefilearg( argcount, args)) != NULL)
     argerrornum( tempname, ME_TOOMANYFILES);

   checknoargs(argcount, args);      /* Check that no arguments are left */

   leftsample = malloc( sizeof(*leftsample) * BUFFSIZE);
   rightsample = malloc( sizeof(*leftsample) * BUFFSIZE);
   stereosample = malloc( sizeof(*leftsample) * 2 * BUFFSIZE);
   if (leftsample == NULL || rightsample == NULL || stereosample == NULL)
     fatalperror ("");

   channels = 2;   /* Output files are stereo */
   if (wavout)
     {
       if ((strcmp(outfilename,"<stdout>")!=0) && (fseek( out, 0, SEEK_SET) != 0)) 
    	 fatalerror("Couldn't navigate output file '%s': %s\n",outfilename, strerror(errno));
       makewavheader();
     }

   startstopwatch();
   while (TRUE)
   {
      maxk = 0;
      for (i = 0; i < 2; i++)
	{
	  k[i] = fread(i==0? leftsample : rightsample,
		       sizeof(*leftsample),
		       BUFFSIZE,
		       channel[i]);
	  if (k[i] == -1)
	    fatalerror("Error reading file '%s': %s\n", filename[i],strerror(errno));
	  if (k[i] > maxk)
	    maxk = k[i];
	}
      if (maxk == 0)
	myexit (0);

      /*-------------------------------------------------*
       * First the left channel as far as it goes ...    *
       *-------------------------------------------------*/
      for (i = 0; i < k[0]; i++)
	stereosample[2 * i] = leftsample[i];
      /*-------------------------------------------------*
       * ... and fill up till the end of this buffer.    *
       *-------------------------------------------------*/
      for (; i < maxk; i++)
	stereosample[2 * i] = 0;

      /*-------------------------------------------------*
       * Next the right channel as far as it goes ...    *
       *-------------------------------------------------*/
      for (i = 0; i < k[1]; i++)
	stereosample[2 * i + 1] = rightsample[i];
      /*-------------------------------------------------*
       * ... and fill up till the end of this buffer.    *
       *-------------------------------------------------*/
      for (; i < maxk; i++)
	stereosample[2 * i + 1] = 0;

      fwrite(stereosample, sizeof(*leftsample), 2 * maxk, out);
      if (ferror( out) != 0)
	fatalerror("Error writing to file '%s': %s\n",
		   outfilename, strerror(errno));
   }
   /* That was an endless loop. This point is never reached. */
}
