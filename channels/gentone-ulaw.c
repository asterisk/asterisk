/* Generate a header file for a particular 
   single or double frequency */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#define CLIP 32635
#define BIAS 0x84
static float loudness=16384.0;

static int calc_samples(int freq)
{
	int x, samples;
	/* Calculate the number of samples at 8000hz sampling
	   we need to have this wave form */
	samples = 8000;
	/* Take out common 2's up to six times */
	for (x=0;x<6;x++) 
		if (!(freq % 2)) {
			freq /= 2;
			samples /= 2;
		}
	/* Take out common 5's (up to three times */
	for (x=0;x<3;x++) 
		if (!(freq % 5)) {
			freq /= 5;
			samples /=5;
		}
	/* No more common factors. */
	return samples;
}

/*
** This routine converts from linear to ulaw
**
** Craig Reese: IDA/Supercomputing Research Center
** Joe Campbell: Department of Defense
** 29 September 1989
**
** References:
** 1) CCITT Recommendation G.711  (very difficult to follow)
** 2) "A New Digital Technique for Implementation of Any
**     Continuous PCM Companding Law," Villeret, Michel,
**     et al. 1973 IEEE Int. Conf. on Communications, Vol 1,
**     1973, pg. 11.12-11.17
** 3) MIL-STD-188-113,"Interoperability and Performance Standards
**     for Analog-to_Digital Conversion Techniques,"
**     17 February 1987
**
** Input: Signed 16 bit linear sample
** Output: 8 bit ulaw sample
*/

#define ZEROTRAP    /* turn on the trap as per the MIL-STD */
#define BIAS 0x84   /* define the add-in bias for 16 bit samples */
#define CLIP 32635

static unsigned char linear2ulaw(short sample) {
static int exp_lut[256] = {0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,
                             4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
                             5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
                             5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
                             6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                             6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                             6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                             6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7};
  int sign, exponent, mantissa;
  unsigned char ulawbyte;

  /* Get the sample into sign-magnitude. */
  sign = (sample >> 8) & 0x80;          /* set aside the sign */
  if (sign != 0) sample = -sample;              /* get magnitude */
  if (sample > CLIP) sample = CLIP;             /* clip the magnitude */

  /* Convert from 16 bit linear to ulaw. */
  sample = sample + BIAS;
  exponent = exp_lut[(sample >> 7) & 0xFF];
  mantissa = (sample >> (exponent + 3)) & 0x0F;
  ulawbyte = ~(sign | (exponent << 4) | mantissa);
#ifdef ZEROTRAP
  if (ulawbyte == 0) ulawbyte = 0x02;   /* optional CCITT trap */
#endif

  return(ulawbyte);
}

int main(int argc, char *argv[])
{
	FILE *f;
	int freq1, freq2;
	float wlen1, wlen2;
	float val;
	int x, samples1, samples2, samples=0;
	char fn[256];
	if (argc < 3) {
		fprintf(stderr, "Usage: gensound <name> <freq1> [freq2]\n");
		exit(1);
	}
	freq1 = atoi(argv[2]);
	if (argc > 3) 
		freq2 = atoi(argv[3]);
	else
		freq2 = 0;
	wlen1 = 8000.0/(float)freq1;
	samples1 = calc_samples(freq1);
	printf("Wavelength 1 (in samples): %10.5f\n", wlen1);
	printf("Minimum samples (1): %d (%f.3 wavelengths)\n", samples1, samples1 / wlen1);
	if (freq2) {
		wlen2 = 8000.0/(float)freq2;
		samples2 = calc_samples(freq2);
		printf("Wavelength 1 (in samples): %10.5f\n", wlen2);
		printf("Minimum samples (1): %d (%f.3 wavelengths)\n", samples2, samples2 / wlen2);
	}
	samples = samples1;
	if (freq2) {
		while(samples % samples2)
			samples += samples1;
	}
	printf("Need %d samples\n", samples);
	snprintf(fn, sizeof(fn), "%s.h", argv[1]);
	if ((f = fopen(fn, "w"))) {
		if (freq2) 
			fprintf(f, "/* %s: Generated from frequencies %d and %d \n"
			           "   by gentone.  %d samples  */\n", fn, freq1, freq2, samples); 
		else
			fprintf(f, "/* %s: Generated from frequency %d\n"
			           "   by gentone.  %d samples  */\n", fn, freq1, samples); 
		fprintf(f, "static unsigned char %s[%d] = {\n\t", argv[1], samples);
		for (x=0;x<samples;x++) {
			val = loudness * sin((freq1 * 2.0 * M_PI * x)/8000.0);
			if (freq2)
				val += loudness * sin((freq2 * 2.0 * M_PI * x)/8000.0);
			fprintf(f, "%3d, ", (int) linear2ulaw(val));
			if (!((x+1) % 8)) 
				fprintf(f, "\n\t");
		}
		if (x % 15)
			fprintf(f, "\n");
		fprintf(f, "};\n");
		fclose(f);
		printf("Wrote %s\n", fn);
	} else {
		fprintf(stderr, "Unable to open %s for writing\n", fn);
		return 1;
	}
	return 0;
}   
