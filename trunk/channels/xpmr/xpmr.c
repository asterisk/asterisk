/*
 * xpmr.c - Xelatec Private Mobile Radio Processes
 * 
 * All Rights Reserved. Copyright (C)2007, Xelatec, LLC
 * 
 * 20070808 1235 Steven Henke, W9SH, sph@xelatec.com
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Private Land Mobile Radio Channel Voice and Signaling Processor
 *
 * \author Steven Henke, W9SH <sph@xelatec.com> Xelatec, LLC
 */
/*
	FYI 	= For Your Information
	PMR 	= Private Mobile Radio
	RX  	= Receive
	TX  	= Transmit
	CTCSS	= Continuous Tone Coded Squelch System
	TONE	= Same as above.
	LSD 	= Low Speed Data, subaudible signaling. May be tones or codes.
	VOX 	= Voice Operated Transmit
	DSP 	= Digital Signal Processing
	LPF 	= Low Pass Filter
	FIR 	= Finite Impulse Response (Filter)
	IIR 	= Infinite Impulse Response (Filter)
*/
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/time.h>
#include <stdlib.h>
#include <errno.h>

#include "xpmr.h"
#include "xpmr_coef.h"
#include "sinetabx.h"

static i16 pmrChanIndex=0;	 			// count of created pmr instances

/*
	Convert a Frequency in Hz to a zero based CTCSS Table index
*/
i16 CtcssFreqIndex(float freq)
{
	i16 i,hit=-1;

	for(i=0;i<CTCSS_NUM_CODES;i++){
		if(freq==freq_ctcss[i])hit=i;
	}
	return hit;
}
/*
	pmr_rx_frontend 
	Takes a block of data and low pass filters it.
	Determines the amplitude of high frequency noise for carrier detect.
	Decimates input data to change the rate.
*/
i16 pmr_rx_frontend(t_pmr_sps *mySps) 
{
	#define DCgainBpfNoise 	65536
 
	i16 samples,iOutput, *input, *output, *noutput;
	i16 *x, *coef, *coef2;
    i32 i, naccum, outputGain, calcAdjust;
	i64 y;
	i16 nx, hyst, setpt, compOut;
	i16 amax, amin, apeak, discounteru, discounterl, discfactor;
	i16 decimator, decimate, doNoise;

	TRACEX(("pmr_rx_frontend()\n"));

	if(!mySps->enabled)return(1);

	decimator = mySps->decimator;
	decimate = mySps->decimate;

	input     = mySps->source;
	output    = mySps->sink;
	noutput   = mySps->parentChan->pRxNoise;

	nx        = mySps->nx;
	coef      = mySps->coef;
	coef2     = mySps->coef2;

	calcAdjust = mySps->calcAdjust;
	outputGain = mySps->outputGain;

	amax=mySps->amax;
	amin=mySps->amin;
	apeak=mySps->apeak;
 	discounteru=mySps->discounteru;
	discounterl=mySps->discounterl;
	discfactor=mySps->discfactor;
	setpt=mySps->setpt;
	hyst=mySps->hyst;
	compOut=mySps->compOut;

	samples=mySps->nSamples*decimate;
	x=mySps->x;		
	iOutput=0;

	if(mySps->parentChan->rxCdType!=CD_XPMR_VOX)doNoise=1;
	else doNoise=0;
			
	for(i=0;i<samples;i++)
	{
		i16 n;

		//shift the old samples
	    for(n=nx-1; n>0; n--)
	       x[n] = x[n-1];

	    x[0] = input[i*2];
	
		--decimator;

		if(decimator<=0)
		{
			decimator=decimate;
			
		    y=0; 
		    for(n=0; n<nx; n++)
		        y += coef[n] * x[n];

		    y=((y/calcAdjust)*outputGain)/M_Q8;

			if(y>32767)y=32767;
			else if(y<-32767)y=-32767;

		    output[iOutput]=y;					// Rx Baseband decimated
			noutput[iOutput++] = apeak;		  	// Rx Noise
		}

		if(doNoise)
		{
			// calculate noise output
			naccum=0;
		    for(n=0; n<nx; n++)
		        naccum += coef_fir_bpf_noise_1[n] * x[n];
		    
		    naccum /= DCgainBpfNoise;
	
			if(naccum>amax)
			{
				amax=naccum;
				discounteru=discfactor;
			}
			else if(--discounteru<=0)
			{
				discounteru=discfactor;
				amax=(i32)((amax*32700)/32768);
			}

			if(naccum<amin)
			{
				amin=naccum;
				discounterl=discfactor;
			}
			else if(--discounterl<=0)
			{
				discounterl=discfactor;
				amin=(i32)((amin*32700)/32768);
			}
		
			apeak=(amax-amin)/2;
			
		}  // if doNoise
	}

	if(doNoise)
	{
		((t_pmr_chan *)(mySps->parentChan))->rxRssi=apeak;
		
		if(apeak>setpt || (compOut&&(apeak>(setpt-hyst)))) compOut=1;
		else compOut=0;
		mySps->compOut=compOut;
		mySps->amax=amax;
		mySps->amin=amin; 				
		mySps->apeak=apeak;
	 	mySps->discounteru=discounteru;
		mySps->discounterl=discounterl; 
	}

	return 0;
}
/*             
	pmr general purpose fir
	works on a block of samples
*/
i16 pmr_gp_fir(t_pmr_sps *mySps) 
{
	i32 nsamples,inputGain,outputGain,calcAdjust;
	i16 *input, *output;
	i16 *x, *coef;
    i32 i, ii;
	i16 nx, hyst, setpt, compOut;
	i16 amax, amin, apeak=0, discounteru=0, discounterl=0, discfactor;
	i16 decimator, decimate, interpolate;
	i16 numChanOut, selChanOut, mixOut, monoOut;

	TRACEX(("pmr_gp_fir() %i\n",mySps->enabled));

	if(!mySps->enabled)return(1);

	inputGain  = mySps->inputGain;
	calcAdjust = mySps->calcAdjust;
	outputGain = mySps->outputGain;

	input      = mySps->source;
	output     = mySps->sink;
	x          = mySps->x;
	nx         = mySps->nx;
	coef       = mySps->coef;
	
	decimator   = mySps->decimator;
	decimate 	= mySps->decimate;
	interpolate = mySps->interpolate;

	setpt	   = mySps->setpt;
	compOut    = mySps->compOut;

	inputGain  = mySps->inputGain;
	outputGain = mySps->outputGain;
	numChanOut = mySps->numChanOut;
	selChanOut = mySps->selChanOut;
	mixOut     = mySps->mixOut;
	monoOut    = mySps->monoOut;

	amax=mySps->amax;
	amin=mySps->amin; 				

	discfactor=mySps->discfactor;
	hyst=mySps->hyst; 				
	setpt=mySps->setpt; 	
	nsamples=mySps->nSamples;

	if(mySps->option==3)
	{
		mySps->option=0;
		mySps->enabled=0;
		for(i=0;i<nsamples;i++)
		{
			if(monoOut)
				output[(i*2)]=output[(i*2)+1]=0;
			else
				output[(i*numChanOut)+selChanOut]=0;
		}
		return 0;
	}

	ii=0;
	for(i=0;i<nsamples;i++)
	{
		int ix;

		int64_t y=0;
		
		if(decimate<0)
		{
			decimator=decimate;	
		}

		for(ix=0;ix<interpolate;ix++)
		{
			i16 n; 
			y=0;
	
		    for(n=nx-1; n>0; n--)
		       x[n] = x[n-1];
		    x[0] = (input[i]*inputGain)/M_Q8;
		
			#if 0
			--decimator;
			if(decimator<=0)
			{
				decimator=decimate;
			    for(n=0; n<nx; n++)
			        y += coef[n] * x[n];
				y /= (outputGain*3);  
				output[ii++]=y;
			}
		 	#else
		    for(n=0; n<nx; n++)
		        y += coef[n] * x[n];

			y=((y/calcAdjust)*outputGain)/M_Q8;
			
			if(mixOut){
				if(monoOut){
					output[(ii*2)]=output[(ii*2)+1]+=y;
				}
				else{
					output[(ii*numChanOut)+selChanOut]+=y;	
				}
			}
			else{
				if(monoOut){
					output[(ii*2)]=output[(ii*2)+1]=y;
				}
				else{
					output[(ii*numChanOut)+selChanOut]=y;
				}
			}
			ii++;
		    #endif
		}

		// amplitude detector
		if(setpt)
		{
			i16 accum=y;

			if(accum>amax)
			{
				amax=accum;
				discounteru=discfactor;
			}
			else if(--discounteru<=0)
			{
				discounteru=discfactor;
				amax=(i32)((amax*32700)/32768);
			}
	
			if(accum<amin)
			{
				amin=accum;
				discounterl=discfactor;
			}
			else if(--discounterl<=0)
			{
				discounterl=discfactor;
				amin=(i32)((amin*32700)/32768);
			}
	
			apeak = (i32)(amax-amin)/2;
			 
 			if(apeak>setpt)compOut=1;
			else if(compOut&&(apeak<(setpt-hyst)))compOut=0;
		}
	}

	mySps->decimator = decimator;

	mySps->amax=amax;
	mySps->amin=amin; 				
	mySps->apeak=apeak;  
 	mySps->discounteru=discounteru;
	mySps->discounterl=discounterl; 
	
	mySps->compOut=compOut;
	
	return 0;
}
/*
	general purpose integrator lpf
*/
i16 gp_inte_00(t_pmr_sps *mySps)
{
	i16 npoints;
 	i16 *input, *output;

	i32 inputGain, outputGain,calcAdjust;
	i32	i;
	i32 accum;

	i32 state00;
 	i16 coeff00, coeff01;

	TRACEX(("gp_inte_00() %i\n",mySps->enabled));
	if(!mySps->enabled)return(1);

	input   = mySps->source;
	output	= mySps->sink;

	npoints=mySps->nSamples;

	inputGain=mySps->inputGain;
	outputGain=mySps->outputGain;
	calcAdjust=mySps->calcAdjust;

	coeff00=((i16*)mySps->coef)[0];
	coeff01=((i16*)mySps->coef)[1];
	state00=((i32*)mySps->x)[0];

	// note fixed gain of 2 to compensate for attenuation
	// in passband

	for(i=0;i<npoints;i++)
	{
		accum=input[i];
		state00 = accum + (state00*coeff01)/M_Q15;
		accum = (state00*coeff00)/(M_Q15/4);
		output[i]=(accum*outputGain)/M_Q8;
	}

	((i32*)(mySps->x))[0]=state00;

	return 0;
}
/*
	general purpose differentiator hpf
*/
i16 gp_diff(t_pmr_sps *mySps)
{
 	i16 *input, *output;
	i16 npoints;
	i32 inputGain, outputGain, calcAdjust;
	i32	i;
	i32 temp0,temp1;
 	i16 x0;
	i32 y0;
	i16 a0,a1;
	i16 b0;
	i16 *coef;
	i16 *x;

	input   = mySps->source;
	output	= mySps->sink;

	npoints=mySps->nSamples;

	inputGain=mySps->inputGain;
	outputGain=mySps->outputGain;
	calcAdjust=mySps->calcAdjust;

	coef=(i16*)(mySps->coef);
	x=(i16*)(mySps->x);
	a0=coef[0];
	a1=coef[1];
	b0=coef[2];

	x0=x[0];

	TRACEX(("gp_diff()\n"));

  	for (i=0;i<npoints;i++)
    {
		temp0 =	x0 * a1;
		   x0 = input[i];
		temp1 = input[i] * a0;
		   y0 = (temp0 + temp1)/calcAdjust;
		output[i]=(y0*outputGain)/M_Q8;
    }

	x[0]=x0;

	return 0;
}
/* 	----------------------------------------------------------------------
	CenterSlicer
*/
i16 CenterSlicer(t_pmr_sps *mySps)
{
	i16 npoints,lhit,uhit;
 	i16 *input, *output, *buff;

	i32 inputGain, outputGain, inputGainB;
	i32	i;
	i32 accum;

	i32  amax;			// buffer amplitude maximum
	i32  amin;			// buffer amplitude minimum
	i32  apeak;			// buffer amplitude peak
	i32  center;
	i32  setpt;			// amplitude set point for peak tracking

	i32  discounteru;	// amplitude detector integrator discharge counter upper
	i32  discounterl;	// amplitude detector integrator discharge counter lower
	i32  discfactor;	// amplitude detector integrator discharge factor

	TRACEX(("CenterSlicer() %i\n",mySps->enabled));

	input   = mySps->source;
	output	= mySps->sink;
	buff    = mySps->buff;

	npoints=mySps->nSamples;

	inputGain=mySps->inputGain;
	outputGain=mySps->outputGain;
	inputGainB=mySps->inputGainB;

	amax=mySps->amax;
	amin=mySps->amin;
	setpt=mySps->setpt;
	apeak=mySps->apeak;
	discounteru=mySps->discounteru;
	discounterl=mySps->discounterl;

	discfactor=mySps->discfactor;
	npoints=mySps->nSamples;

	for(i=0;i<npoints;i++)
	{
		accum=input[i];

		lhit=uhit=0;

		if(accum>amax)
		{
			amax=accum;
			uhit=1;
			if(amin<(amax-setpt))
			{
				amin=(amax-setpt);
				lhit=1;
			}
		}
		else if(accum<amin)
		{
			amin=accum;
			lhit=1;
			if(amax>(amin+setpt))
			{
				amax=(amin+setpt);
				uhit=1;
			}
		}
	
		if(--discounteru<=0 && amax>0)
		{
			amax--;
			uhit=1;
		}
	
		if(--discounterl<=0 && amin<0)
		{
			amin++;
			lhit=1;
		}
	
		if(uhit)discounteru=discfactor;	
		if(lhit)discounterl=discfactor;	
		
		apeak = (amax-amin)/2;
		center = (amax+amin)/2;
		accum = accum - center;
		output[i]=accum;
	
		// do limiter function
		if(accum>inputGainB)accum=inputGainB;
		else if(accum<-inputGainB)accum=-inputGainB;
		buff[i]=accum;

		#if XPMR_DEBUG0 == 1
		#if 0
		mySps->debugBuff0[i]=center;
		#endif
	    #if 0
		if(mySps->parentChan->frameCountRx&0x01) mySps->parentChan->prxDebug1[i]=amax;
		else mySps->parentChan->prxDebug1[i]=amin;
		#endif
		#endif
	}

	mySps->amax=amax;
	mySps->amin=amin;
	mySps->apeak=apeak;
	mySps->discounteru=discounteru;
	mySps->discounterl=discounterl;

	return 0;
}
/* 	----------------------------------------------------------------------
	MeasureBlock
	determine peak amplitude
*/
i16 MeasureBlock(t_pmr_sps *mySps)
{
	i16 npoints;
 	i16 *input, *output;

	i32 inputGain, outputGain;
	i32	i;
	i32 accum;

	i16  amax;			// buffer amplitude maximum
	i16  amin;			// buffer amplitude minimum
	i16  apeak=0;			// buffer amplitude peak (peak to peak)/2
	i16  setpt;			// amplitude set point for amplitude comparator

	i32  discounteru;	// amplitude detector integrator discharge counter upper
	i32  discounterl;	// amplitude detector integrator discharge counter lower
	i32  discfactor;	// amplitude detector integrator discharge factor

	TRACEX(("MeasureBlock() %i\n",mySps->enabled));

	if(!mySps->enabled)return 1;

	if(mySps->option==3)
	{
		mySps->amax = mySps->amin = mySps->apeak = \
		mySps->discounteru = mySps->discounterl = \
		mySps->enabled = 0;
		return 1;
	}

	input   = mySps->source;
	output	= mySps->sink;

	npoints=mySps->nSamples;

	inputGain=mySps->inputGain;
	outputGain=mySps->outputGain;

	amax=mySps->amax;
	amin=mySps->amin;
	setpt=mySps->setpt;
	discounteru=mySps->discounteru;
	discounterl=mySps->discounterl;

	discfactor=mySps->discfactor;
	npoints=mySps->nSamples;

	for(i=0;i<npoints;i++)
	{
		accum=input[i];

		if(accum>amax)
		{
			amax=accum;
			discounteru=discfactor;
		}
		else if(--discounteru<=0)
		{
			discounteru=discfactor;
			amax=(i32)((amax*32700)/32768);
		}

		if(accum<amin)
		{
			amin=accum;
			discounterl=discfactor;
		}
		else if(--discounterl<=0)
		{
			discounterl=discfactor;
			amin=(i32)((amin*32700)/32768);
		}

		apeak = (i32)(amax-amin)/2;
		if(output)output[i]=apeak;
	}
    
	mySps->amax=amax;
	mySps->amin=amin;
	mySps->apeak=apeak;
	mySps->discounteru=discounteru;
	mySps->discounterl=discounterl;
	if(apeak>=setpt) mySps->compOut=1;
	else mySps->compOut=0;
	
	//TRACEX((" -MeasureBlock()=%i\n",mySps->apeak));
	return 0;
}
/*
	SoftLimiter
*/
i16 SoftLimiter(t_pmr_sps *mySps)
{
	i16 npoints;
	//i16 samples, lhit,uhit;
 	i16 *input, *output;

	i32 inputGain, outputGain;
	i32	i;
	i32 accum;
	i32  tmp;

	i32  amax;			// buffer amplitude maximum
	i32  amin;			// buffer amplitude minimum
	//i32  apeak;		// buffer amplitude peak
	i32  setpt;			// amplitude set point for amplitude comparator
	i16  compOut;		// amplitude comparator output

	input   = mySps->source;
	output	= mySps->sink;

	inputGain=mySps->inputGain;
	outputGain=mySps->outputGain;

	npoints=mySps->nSamples;

	setpt=mySps->setpt;
	amax=(setpt*124)/128;
	amin=-amax;

	TRACEX(("SoftLimiter() %i %i %i) \n",amin, amax,setpt));

	for(i=0;i<npoints;i++)
	{
		accum=input[i];
		//accum=input[i]*mySps->inputGain/256;

		if(accum>setpt)
		{
		    tmp=((accum-setpt)*4)/128;
		    accum=setpt+tmp;
			if(accum>amax)accum=amax;
			compOut=1;
			accum=setpt;
		}
		else if(accum<-setpt)
		{
		    tmp=((accum+setpt)*4)/128;
		    accum=(-setpt)-tmp;
			if(accum<amin)accum=amin;
			compOut=1;
			accum=-setpt;
		}

		output[i]=(accum*outputGain)/M_Q8;
	}

	return 0;
}
/*
	SigGen() - sine, square function generator
	sps overloaded values
	discfactor  = phase factor
	discfactoru = phase index
	if source is not NULL then mix it in!

	sign table and output gain are in Q15 format (32767=.999)
*/
i16	SigGen(t_pmr_sps *mySps)
{
	#define PH_FRACT_FACT	128

	i32 ph;
	i16 i,outputgain,waveform,numChanOut,selChanOut;
	i32 accum;				 

	TRACEX(("SigGen(%i) \n",mySps->option));

	if(!mySps->freq ||!mySps->enabled)return 0;

	outputgain=mySps->outputGain;
	waveform=0;
	numChanOut=mySps->numChanOut; 
	selChanOut=mySps->selChanOut;

    if(mySps->option==1)
	{
		mySps->option=0;
		mySps->state=1;
		mySps->discfactor=
			(SAMPLES_PER_SINE*mySps->freq*PH_FRACT_FACT)/mySps->sampleRate/10;
	
		TRACEX((" SigGen() discfactor = %i\n",mySps->discfactor));
		if(mySps->discounterl)mySps->state=2;
	}
	else if(mySps->option==2)
	{
		i16 shiftfactor=CTCSS_TURN_OFF_SHIFT;
		// phase shift request
		mySps->option=0;
		mySps->state=2;
		mySps->discounterl=CTCSS_TURN_OFF_TIME-(2*MS_PER_FRAME);   		// 

		mySps->discounteru = \
			(mySps->discounteru + (((SAMPLES_PER_SINE*shiftfactor)/360)*PH_FRACT_FACT)) % (SAMPLES_PER_SINE*PH_FRACT_FACT);
		//printf("shiftfactor = %i\n",shiftfactor);
		//shiftfactor+=10;
	}
	else if(mySps->option==3)
	{
		// stop it and clear the output buffer
		mySps->option=0;
		mySps->state=0;
		mySps->enabled=0;
		for(i=0;i<mySps->nSamples;i++)
			mySps->sink[(i*numChanOut)+selChanOut]=0;
		return(0);
	}
	else if(mySps->state==2)
	{
		// doing turn off
		mySps->discounterl-=MS_PER_FRAME;
		if(mySps->discounterl<=0)
		{
			mySps->option=3;
			mySps->state=2;
		}
	}
	else if(mySps->state==0)
	{
		return(0);
	}

	ph=mySps->discounteru;

	for(i=0;i<mySps->nSamples;i++)
	{
		if(!waveform)
		{
			// sine
			//tmp=(sinetablex[ph/PH_FRACT_FACT]*amplitude)/M_Q16;
			accum=sinetablex[ph/PH_FRACT_FACT];
			accum=(accum*outputgain)/M_Q8;
	    }
		else
		{
			// square
			if(ph>SAMPLES_PER_SINE/2)
				accum=outputgain/M_Q8;
			else
				accum=-outputgain/M_Q8;
		}

		if(mySps->source)accum+=mySps->source[i];

		mySps->sink[(i*numChanOut)+selChanOut]=accum;

		ph=(ph+mySps->discfactor)%(SAMPLES_PER_SINE*PH_FRACT_FACT);
	}

	mySps->discounteru=ph;

	return 0;
}
/*
	adder/mixer
	takes existing buffer and adds source buffer to destination buffer
	sink buffer = (sink buffer * gain) + source buffer 
*/
i16 pmrMixer(t_pmr_sps *mySps)
{
	i32 accum;
	i16 i, *input, *inputB, *output;
	i16  inputGain, inputGainB;	  	// apply to input data	 in Q7.8 format
	i16  outputGain;	// apply to output data  in Q7.8 format
	i16	 discounteru,discounterl,amax,amin,setpt,discfactor;
	i16	 npoints,uhit,lhit,apeak,measPeak;

	TRACEX(("pmrMixer()\n"));

	input     = mySps->source;
	inputB    = mySps->sourceB;
	output    = mySps->sink;

	inputGain=mySps->inputGain;
	inputGainB=mySps->inputGainB;
	outputGain=mySps->outputGain;
	
	amax=mySps->amax;
	amin=mySps->amin;
	setpt=mySps->setpt;
	discounteru=mySps->discounteru;
	discounterl=mySps->discounteru;

	discfactor=mySps->discfactor;
	npoints=mySps->nSamples;
	measPeak=mySps->measPeak;

	for(i=0;i<npoints;i++)
	{
		accum = ((input[i]*inputGain)/M_Q8) + 
				((inputB[i]*inputGainB)/M_Q8);

		accum=(accum*outputGain)/M_Q8;
		output[i]=accum;

		if(measPeak){
	  		lhit=uhit=0;
	
			if(accum>amax){
				amax=accum;
				uhit=1;
				if(amin<(amax-setpt)){
					amin=(amax-setpt);
					lhit=1;
				}
			}
			else if(accum<amin){
				amin=accum;
				lhit=1;
				if(amax>(amin+setpt)){
					amax=(amin+setpt);
					uhit=1;
				}
			}
		
			if(--discounteru<=0 && amax>0){
				amax--;
				uhit=1;
			}
		
			if(--discounterl<=0 && amin<0){
				amin++;
				lhit=1;
			}
		
			if(uhit)discounteru=discfactor;	
			if(lhit)discounterl=discfactor;
		}	
 	}

	if(measPeak){
		apeak = (amax-amin)/2;
		mySps->apeak=apeak;
		mySps->amax=amax;
		mySps->amin=amin;
		mySps->discounteru=discounteru;
		mySps->discounterl=discounterl;
	}
	
	return 0;
}
/*
	DelayLine 
*/
i16 DelayLine(t_pmr_sps *mySps)
{
	i16 *input, *output, *buff;
	i16	 i, npoints,buffsize,inindex,outindex;

	TRACEX((" DelayLine() %i\n",mySps->enabled));
	
	input    	= mySps->source;
	output    	= mySps->sink;
	buff     	= (i16*)(mySps->buff);
	buffsize  	= mySps->buffSize;
	npoints		= mySps->nSamples;

	outindex	= mySps->buffOutIndex;
	inindex		= outindex + mySps->buffLead;

	for(i=0;i<npoints;i++)
	{
		inindex %= buffsize;
		outindex %= buffsize;
		
		buff[inindex]=input[i];
		output[i]=buff[outindex];
		inindex++;
		outindex++;
 	}
	mySps->buffOutIndex=outindex;

 	return 0;
}
/*
	Continuous Tone Coded Squelch (CTCSS) Detector
*/
i16 ctcss_detect(t_pmr_chan *pmrChan)
{
	i16 i,points2do, points=0, *pInput, hit, thit,relax; 
	i16 tnum, tmp, indexWas=0, indexNow, gain, peakwas=0, diffpeak;
	i16 difftrig;
	i16 lasttv0=0, lasttv1=0, lasttv2=0, tv0, tv1, tv2, indexDebug;

	TRACEX(("ctcss_detect(%p) %i %i %i %i\n",pmrChan, 
		pmrChan->rxCtcss->enabled,
		pmrChan->rxCtcssIndex,
		pmrChan->rxCtcss->testIndex,
		pmrChan->rxCtcss->decode));

	if(!pmrChan->rxCtcss->enabled)return(1);

	relax  = pmrChan->rxCtcss->relax; 
	pInput = pmrChan->rxCtcss->input;
	gain   = pmrChan->rxCtcss->gain;
	
	if(relax) difftrig=(-0.1*M_Q15);
	else difftrig=(-0.05*M_Q15);

	thit=hit=-1;

	//TRACEX((" ctcss_detect() %i  %i  %i  %i\n", CTCSS_NUM_CODES,0,0,0));

	for(tnum=0;tnum<CTCSS_NUM_CODES;tnum++)
	{
		i32 accum, peak;
		t_tdet	*ptdet;
		i16 fudgeFactor;
		i16 binFactor;

		//TRACEX((" ctcss_detect() tnum=%i %i\n",tnum,pmrChan->rxCtcssMap[tnum]));

		if( (pmrChan->rxCtcssMap[tnum] < 0) || 
		    (pmrChan->rxCtcss->decode>=0 && (tnum!= pmrChan->rxCtcss->decode)) ||
			(!pmrChan->rxCtcss->multiFreq && (tnum!= pmrChan->rxCtcssIndex))
		  )
			continue;

		//TRACEX((" ctcss_detect() tnum=%i\n",tnum));

		ptdet=&(pmrChan->rxCtcss->tdet[tnum]);
		indexDebug=0;
		points=points2do=pmrChan->nSamplesRx;
		fudgeFactor=ptdet->fudgeFactor;
		binFactor=ptdet->binFactor;

		while(ptdet->counter < (points2do*CTCSS_SCOUNT_MUL))
		{
			//TRACEX((" ctcss_detect() - inner loop\n"));
			tmp=(ptdet->counter/CTCSS_SCOUNT_MUL)+1;
		    ptdet->counter-=(tmp*CTCSS_SCOUNT_MUL);
			points2do-=tmp;
			indexNow=points-points2do;
			
			ptdet->counter += ptdet->counterFactor;

			accum = pInput[indexNow-1];	 	// dude's major bug fix!

			peakwas=ptdet->peak;

			ptdet->z[ptdet->zIndex]+=
				(((accum - ptdet->z[ptdet->zIndex])*binFactor)/M_Q15);

			peak = abs(ptdet->z[0]-ptdet->z[2]) + abs(ptdet->z[1]-ptdet->z[3]);

			if (ptdet->peak < peak)
				ptdet->peak += ( ((peak-ptdet->peak)*binFactor)/M_Q15);
			else
				ptdet->peak=peak;

			{
				static const i16 a0=13723;
				static const i16 a1=-13723;
				i32 temp0,temp1;
				i16 x0;
	
				//differentiate
				x0=ptdet->zd;
				temp0 =	x0 * a1;
				ptdet->zd = ptdet->peak;
				temp1 = ptdet->peak * a0;
			    diffpeak = (temp0 + temp1)/1024;
			}

			if(diffpeak<(-0.03*M_Q15))ptdet->dvd-=4;
			else if(ptdet->dvd<0)ptdet->dvd++;

			if((ptdet->dvd < -12) && diffpeak > (-0.02*M_Q15))ptdet->dvu+=2;
			else if(ptdet->dvu)ptdet->dvu--;

			tmp=ptdet->setpt;
			if(pmrChan->rxCtcss->decode==tnum)
			{
				if(relax)tmp=(tmp*55)/100;
				else tmp=(tmp*80)/100;
			}

			if(ptdet->peak > tmp)
			{
			    if(ptdet->decode<(fudgeFactor*32))ptdet->decode++;
			}
			else if(pmrChan->rxCtcss->decode==tnum)
			{
				if(ptdet->peak > ptdet->hyst)ptdet->decode--;
				else if(relax) ptdet->decode--;	
				else ptdet->decode-=4; 
			}
			else
			{
				ptdet->decode=0;
			}

			if((pmrChan->rxCtcss->decode==tnum) && !relax && (ptdet->dvu > (0.00075*M_Q15)))
			{
				ptdet->decode=0;
				ptdet->z[0]=ptdet->z[1]=ptdet->z[2]=ptdet->z[3]=ptdet->dvu=0;
				//printf("ctcss_detect() turnoff code!\n");
			}

			if(ptdet->decode<0 || !pmrChan->rxCarrierDetect)ptdet->decode=0;

			if(ptdet->decode>=fudgeFactor)thit=tnum;  

			#if XPMR_DEBUG0 == 1
			//if(thit>=0 && thit==tnum)
			//	printf(" ctcss_detect() %i %i %i %i \n",tnum,ptdet->peak,ptdet->setpt,ptdet->hyst);

			// tv0=accum;
			tv0=ptdet->peak;
			tv1=diffpeak;
			tv2=ptdet->dvu;
			
			//tv1=ptdet->zi*100;
			while(indexDebug<indexNow)
			{
				if(indexDebug==0)lasttv0=ptdet->pDebug0[points-1];
				if(ptdet->pDebug0)ptdet->pDebug0[indexDebug]=lasttv0;

				if(indexDebug==0)lasttv1=ptdet->pDebug1[points-1];
				if(ptdet->pDebug1)ptdet->pDebug1[indexDebug]=lasttv1;

				if(indexDebug==0)lasttv2=ptdet->pDebug2[points-1];
				if(ptdet->pDebug2)ptdet->pDebug2[indexDebug]=lasttv2;

				indexDebug++;
			}
			lasttv0=tv0;
			lasttv1=tv1;
			lasttv2=tv2*100;
			#endif
			indexWas=indexNow;
			ptdet->zIndex=(++ptdet->zIndex)%4;
		}
		ptdet->counter-=(points2do*CTCSS_SCOUNT_MUL);

		#if XPMR_DEBUG0 == 1
		for(i=indexWas;i<points;i++)
		{
			if(ptdet->pDebug0)ptdet->pDebug0[i]=lasttv0;
			if(ptdet->pDebug1)ptdet->pDebug1[i]=lasttv1;
			if(ptdet->pDebug2)ptdet->pDebug2[i]=lasttv2;
		}
		#endif
	}

	//TRACEX((" ctcss_detect() thit %i\n",thit));

	if(pmrChan->rxCtcss->BlankingTimer>0)pmrChan->rxCtcss->BlankingTimer-=points;
	if(pmrChan->rxCtcss->BlankingTimer<0)pmrChan->rxCtcss->BlankingTimer=0;

    if(thit>=0 && pmrChan->rxCtcss->decode<0 && !pmrChan->rxCtcss->BlankingTimer)
    {
		pmrChan->rxCtcss->decode=thit;		
	}
	else if(thit<0 && pmrChan->rxCtcss->decode>=0)
	{
		pmrChan->rxCtcss->BlankingTimer=SAMPLE_RATE_NETWORK/5;
		pmrChan->rxCtcss->decode=-1;	

		for(tnum=0;tnum<CTCSS_NUM_CODES;tnum++)
		{
		    t_tdet	*ptdet=NULL;
			ptdet=&(pmrChan->rxCtcss->tdet[tnum]);
		    ptdet->decode=0;
			ptdet->z[0]=ptdet->z[1]=ptdet->z[2]=ptdet->z[3]=0;
		}
	}
	//TRACEX((" ctcss_detect() thit %i %i\n",thit,pmrChan->rxCtcss->decode));
	return(0);
}
/*
	TxTestTone
*/
static i16	TxTestTone(t_pmr_chan *pChan, i16 function)
{
	if(function==1)
	{
		pChan->spsSigGen1->enabled=1;
		pChan->spsSigGen1->option=1;
		pChan->spsTx->source=pChan->spsSigGen1->sink;
	}
	else
	{
		pChan->spsSigGen1->option=3;
	}
	return 0;
}
/*	
	assumes:
	sampling rate is 48KS/s
	samples are all 16 bits
    samples are filtered and decimated by 1/6th
*/
t_pmr_chan	*createPmrChannel(t_pmr_chan *tChan, i16 numSamples)
{
	i16 i, *inputTmp;

	t_pmr_chan 	*pChan;
	t_pmr_sps  	*pSps;
	t_dec_ctcss	*pDecCtcss;
	t_tdet     	*ptdet;

	TRACEX(("createPmrChannel(%p,%i)\n",tChan,numSamples));

	pChan = (t_pmr_chan *)calloc(sizeof(t_pmr_chan),1);

	if(pChan==NULL)
	{
		printf("createPmrChannel() failed\n");
		return(NULL);
	}
	
	pChan->nSamplesRx=numSamples;
	pChan->nSamplesTx=numSamples;

	pChan->index=pmrChanIndex++;

	for(i=0;i<CTCSS_NUM_CODES;i++)
	{
		pChan->rxCtcssMap[i]=-1;	
	}

	pChan->rxCtcssIndex=-1;

	if(tChan==NULL)
	{
		pChan->rxNoiseSquelchEnable=0;
		pChan->rxHpfEnable=0;
		pChan->rxDeEmpEnable=0;
		pChan->rxCenterSlicerEnable=0;
		pChan->rxCtcssDecodeEnable=0;
		pChan->rxDcsDecodeEnable=0;

		pChan->rxCarrierPoint = 17000;
		pChan->rxCarrierHyst = 2500;

		pChan->rxCtcssFreq=103.5;

		pChan->txHpfEnable=0;
		pChan->txLimiterEnable=0;
		pChan->txPreEmpEnable=0;
		pChan->txLpfEnable=1;
		pChan->txCtcssFreq=103.5;
		pChan->txMixA=TX_OUT_VOICE;
		pChan->txMixB=TX_OUT_LSD;
	}
	else
	{
		pChan->rxDemod=tChan->rxDemod;
		pChan->rxCdType=tChan->rxCdType;
		pChan->rxSquelchPoint = tChan->rxSquelchPoint;
		pChan->rxCarrierHyst = 3000;
		pChan->rxCtcssFreq=tChan->rxCtcssFreq;

		for(i=0;i<CTCSS_NUM_CODES;i++)
			pChan->rxCtcssMap[i]=tChan->rxCtcssMap[i];
		
		pChan->txMod=tChan->txMod;
		pChan->txHpfEnable=1; 
		pChan->txLpfEnable=1;
		pChan->txCtcssFreq=tChan->txCtcssFreq;
		pChan->txMixA=tChan->txMixA;
		pChan->txMixB=tChan->txMixB;
		pChan->radioDuplex=tChan->radioDuplex;
	}

	TRACEX(("misc settings \n"));

	if(pChan->rxCdType==CD_XPMR_NOISE){
		pChan->rxNoiseSquelchEnable=1;
	}

	if(pChan->rxDemod==RX_AUDIO_FLAT){
		pChan->rxHpfEnable=1;
		pChan->rxDeEmpEnable=1;
	}

	pChan->rxCarrierPoint=(pChan->rxSquelchPoint*32767)/100;
	pChan->rxCarrierHyst = 3000; //pChan->rxCarrierPoint/15;
	
	pChan->rxDcsDecodeEnable=0;

	if(pChan->rxCtcssFreq!=0){
		pChan->rxHpfEnable=1;
		pChan->rxCenterSlicerEnable=1;
		pChan->rxCtcssDecodeEnable=1;
		pChan->rxCtcssIndex=CtcssFreqIndex(pChan->rxCtcssFreq);
	}

	if(pChan->txMod){
		pChan->txPreEmpEnable=1;
		pChan->txLimiterEnable=1;
	}
	
	TRACEX(("calloc buffers \n"));

	pChan->pRxDemod 	= calloc(numSamples,2);
	pChan->pRxNoise 	= calloc(numSamples,2);
	pChan->pRxBase 		= calloc(numSamples,2);
	pChan->pRxHpf 		= calloc(numSamples,2);
	pChan->pRxLsd 		= calloc(numSamples,2);
	pChan->pRxSpeaker 	= calloc(numSamples,2);
	pChan->pRxCtcss 	= calloc(numSamples,2);
	pChan->pRxDcTrack 	= calloc(numSamples,2);
	pChan->pRxLsdLimit 	= calloc(numSamples,2);
	
	
	pChan->pTxBase  	= calloc(numSamples,2);
	pChan->pTxHpf	 	= calloc(numSamples,2);
	pChan->pTxPreEmp 	= calloc(numSamples,2);
	pChan->pTxLimiter 	= calloc(numSamples,2);
	pChan->pTxLsd	 	= calloc(numSamples,2);
	pChan->pTxLsdLpf    = calloc(numSamples,2);
	pChan->pTxComposite	= calloc(numSamples,2);
	pChan->pSigGen0		= calloc(numSamples,2);
    pChan->pSigGen1		= calloc(numSamples,2);

	pChan->pTxCode      = calloc(numSamples,2);
	pChan->pTxOut		= calloc(numSamples,2*2*6);		// output buffer
	
	#if XPMR_DEBUG0 == 1
	pChan->pTxPttIn     = calloc(numSamples,2);
	pChan->pTxPttOut    = calloc(numSamples,2);
	pChan->prxDebug0	= calloc(numSamples,2);
	pChan->prxDebug1	= calloc(numSamples,2);
	pChan->prxDebug2	= calloc(numSamples,2);
	pChan->prxDebug3	= calloc(numSamples,2);
	pChan->ptxDebug0	= calloc(numSamples,2);
	pChan->ptxDebug1	= calloc(numSamples,2);
	pChan->ptxDebug2	= calloc(numSamples,2);
	pChan->ptxDebug3	= calloc(numSamples,2);
	pChan->pNull		= calloc(numSamples,2);
	for(i=0;i<numSamples;i++)pChan->pNull[i]=((i%(numSamples/2))*8000)-4000;
 	#endif

	TRACEX(("create ctcss\n"));

	pDecCtcss = (t_dec_ctcss *)calloc(sizeof(t_dec_ctcss),1);
	 
	pChan->rxCtcss=pDecCtcss; 
	pDecCtcss->enabled=1;
	pDecCtcss->gain=1*M_Q8;
	pDecCtcss->limit=8192;
	pDecCtcss->input=pChan->pRxLsdLimit;
	pDecCtcss->testIndex=pChan->rxCtcssIndex;
	if(!pDecCtcss->testIndex)pDecCtcss->testIndex=1;
	pChan->rxCtcssMap[pChan->rxCtcssIndex]=pChan->rxCtcssIndex;
	pDecCtcss->decode=-1;

	for(i=0;i<CTCSS_NUM_CODES;i++)
	{
		ptdet=&(pChan->rxCtcss->tdet[i]);
		ptdet->state=1;
		ptdet->setpt=(M_Q15*0.067);		   			// 0.069
		ptdet->hyst =(M_Q15*0.020);
		ptdet->counterFactor=coef_ctcss_div[i];
		ptdet->binFactor=(M_Q15*0.135);			  	// was 0.140
		ptdet->fudgeFactor=8; 	
	}

	// General Purpose Function Generator
	pSps=pChan->spsSigGen1=createPmrSps();
	pSps->parentChan=pChan;
	pSps->sink=pChan->pSigGen1; 
	pSps->numChanOut=1;
	pSps->selChanOut=0;
	pSps->sigProc=SigGen; 
	pSps->nSamples=pChan->nSamplesTx;
	pSps->sampleRate=SAMPLE_RATE_NETWORK;
	pSps->freq=10000; 						// in increments of 0.1 Hz
	pSps->outputGain=(.25*M_Q8);
	pSps->option=0;
	pSps->interpolate=1;
	pSps->decimate=1;
	pSps->enabled=0;


	// CTCSS ENCODER
	pSps = pChan->spsSigGen0 = createPmrSps();
	pSps->parentChan=pChan;
	pSps->sink=pChan->pTxLsd;  
	pSps->sigProc=SigGen; 
	pSps->numChanOut=1;
	pSps->selChanOut=0;
	pSps->nSamples=pChan->nSamplesTx;
	pSps->sampleRate=SAMPLE_RATE_NETWORK;
	pSps->freq=pChan->txCtcssFreq*10;		// in increments of 0.1 Hz
	pSps->outputGain=(0.5*M_Q8);
	pSps->option=0;
	pSps->interpolate=1;
	pSps->decimate=1;
	pSps->enabled=0;


	// Tx LSD Low Pass Filter
	pSps=pChan->spsTxLsdLpf=pSps->nextSps=createPmrSps();
	pSps->source=pChan->pTxLsd;
	pSps->sink=pChan->pTxLsdLpf;  
	pSps->sigProc=pmr_gp_fir;
	pSps->enabled=0;
	pSps->numChanOut=1;
	pSps->selChanOut=0;
	pSps->nSamples=pChan->nSamplesTx;
	pSps->decimator=pSps->decimate=1;
	pSps->interpolate=1;
	pSps->inputGain=(1*M_Q8);
	pSps->outputGain=(1*M_Q8);

	if(pChan->txCtcssFreq>203.0)
	{
		pSps->ncoef=taps_fir_lpf_250_9_66;
		pSps->size_coef=2;
		pSps->coef=(void*)coef_fir_lpf_250_9_66;
		pSps->nx=taps_fir_lpf_250_9_66;
		pSps->size_x=2;
		pSps->x=(void*)(calloc(pSps->nx,pSps->size_x));
		pSps->calcAdjust=gain_fir_lpf_250_9_66;
	}
	else
	{
		pSps->ncoef=taps_fir_lpf_215_9_88;
		pSps->size_coef=2;
		pSps->coef=(void*)coef_fir_lpf_215_9_88;
		pSps->nx=taps_fir_lpf_215_9_88;
		pSps->size_x=2;
		pSps->x=(void*)(calloc(pSps->nx,pSps->size_x));
		pSps->calcAdjust=gain_fir_lpf_215_9_88;
	}

	pSps->inputGain=(1*M_Q8);
	pSps->outputGain=(1*M_Q8);
	
	if(pSps==NULL)printf("Error: calloc(), createPmrChannel()\n"); 

 		
	// RX Process
	TRACEX(("create rx\n"));
	pSps = NULL;

	// allocate space for first sps and set pointers
	pSps=pChan->spsRx=createPmrSps();
	pSps->parentChan=pChan;
	pSps->source=NULL;					//set when called
	pSps->sink=pChan->pRxBase;
	pSps->sigProc=pmr_rx_frontend;
	pSps->enabled=1;
	pSps->decimator=pSps->decimate=6;
	pSps->interpolate=pSps->interpolate=1;
	pSps->nSamples=pChan->nSamplesRx;
	pSps->ncoef=taps_fir_bpf_noise_1;
	pSps->size_coef=2;
	pSps->coef=(void*)coef_fir_lpf_3K_1;
	pSps->coef2=(void*)coef_fir_bpf_noise_1;
	pSps->nx=taps_fir_bpf_noise_1;
	pSps->size_x=2;
	pSps->x=(void*)(calloc(pSps->nx,pSps->size_coef));
	pSps->calcAdjust=(gain_fir_lpf_3K_1*256)/0x0100;
	pSps->outputGain=(1.0*M_Q8);
	pSps->discfactor=2;	  
	pSps->hyst=pChan->rxCarrierHyst;
	pSps->setpt=pChan->rxCarrierPoint;
	pChan->prxSquelchAdjust=&pSps->setpt;
	#if XPMR_DEBUG0 == 1
	pSps->debugBuff0=pChan->pRxDemod;
	pSps->debugBuff1=pChan->pRxNoise;
	pSps->debugBuff2=pChan->prxDebug0;
	#endif

 
	// allocate space for next sps and set pointers
	// Rx SubAudible Decoder Low Pass Filter
	pSps=pSps->nextSps=createPmrSps();
	pSps->parentChan=pChan;
	pSps->source=pChan->pRxBase;
	pSps->sink=pChan->pRxLsd;
	pSps->sigProc=pmr_gp_fir;
	pSps->enabled=1;
	pSps->numChanOut=1;
	pSps->selChanOut=0;
	pSps->nSamples=pChan->nSamplesRx;
	pSps->decimator=pSps->decimate=1;
	pSps->interpolate=1;

	if(pChan->rxCtcssFreq>203.5)
	{
		pSps->ncoef=taps_fir_lpf_250_9_66;
		pSps->size_coef=2;
		pSps->coef=(void*)coef_fir_lpf_250_9_66;
		pSps->nx=taps_fir_lpf_250_9_66;
		pSps->size_x=2;
		pSps->x=(void*)(calloc(pSps->nx,pSps->size_x));
		pSps->calcAdjust=gain_fir_lpf_250_9_66;
	}
	else
	{
		pSps->ncoef=taps_fir_lpf_215_9_88;
		pSps->size_coef=2;
		pSps->coef=(void*)coef_fir_lpf_215_9_88;
		pSps->nx=taps_fir_lpf_215_9_88;
		pSps->size_x=2;
		pSps->x=(void*)(calloc(pSps->nx,pSps->size_x));
		pSps->calcAdjust=gain_fir_lpf_215_9_88;
	}

	pSps->inputGain=(1*M_Q8);
	pSps->outputGain=(1*M_Q8);
	pChan->prxCtcssMeasure=pSps->sink;
	pChan->prxCtcssAdjust=&(pSps->outputGain);	


 	// allocate space for next sps and set pointers
	// CenterSlicer
	if(pChan->rxCenterSlicerEnable)
	{
		pSps=pSps->nextSps=createPmrSps();
		pSps->parentChan=pChan;
		pSps->source=pChan->pRxLsd;
		pSps->sink=pChan->pRxDcTrack;
		pSps->buff=pChan->pRxLsdLimit;
		pSps->sigProc=CenterSlicer;
		pSps->enabled=1;
		pSps->nSamples=pChan->nSamplesRx;
		pSps->discfactor=800;
		pSps->inputGain=(1*M_Q8);
		pSps->outputGain=(1*M_Q8);
		pSps->setpt=3000;
		pSps->inputGainB=1000; 			// limiter set point
	}

	// allocate space for next sps and set pointers
	// Rx HPF
	pSps=pSps->nextSps=createPmrSps();
	pSps->parentChan=pChan;
	pChan->spsRxHpf=pSps;
	pSps->source=pChan->pRxBase;
	pSps->sink=pChan->pRxHpf;  
	pSps->sigProc=pmr_gp_fir;
	pSps->enabled=1;
	pSps->numChanOut=1;
	pSps->selChanOut=0;
	pSps->nSamples=pChan->nSamplesRx;
	pSps->decimator=pSps->decimate=1;
	pSps->interpolate=1;
	pSps->ncoef=taps_fir_hpf_300_9_66;
	pSps->size_coef=2;
	pSps->coef=(void*)coef_fir_hpf_300_9_66;
	pSps->nx=taps_fir_hpf_300_9_66;
	pSps->size_x=2;
	pSps->x=(void*)(calloc(pSps->nx,pSps->size_x));
	if(pSps==NULL)printf("Error: calloc(), createPmrChannel()\n"); 
	pSps->calcAdjust=gain_fir_hpf_300_9_66;
	pSps->inputGain=(1*M_Q8);
	pSps->outputGain=(1*M_Q8);
	pChan->spsRxOut=pSps;

	// allocate space for next sps and set pointers
	// Rx DeEmp
	if(pChan->rxDeEmpEnable){
		pSps=pSps->nextSps=createPmrSps();
		pSps->parentChan=pChan;
		pChan->spsRxDeEmp=pSps;
		pSps->source=pChan->pRxHpf;
		pSps->sink=pChan->pRxSpeaker;  
		pChan->spsRxOut=pSps;					 // OUTPUT STRUCTURE! maw
		pSps->sigProc=gp_inte_00;
		pSps->enabled=1;
		pSps->nSamples=pChan->nSamplesRx;
	
		pSps->ncoef=taps_int_lpf_300_1_2;
		pSps->size_coef=2;
		pSps->coef=(void*)coef_int_lpf_300_1_2;
	
		pSps->nx=taps_int_lpf_300_1_2;
		pSps->size_x=4;
		pSps->x=(void*)(calloc(pSps->nx,pSps->size_x));
		if(pSps==NULL)printf("Error: calloc(), createPmrChannel()\n"); 
		pSps->calcAdjust=gain_int_lpf_300_1_2/2;
		pSps->inputGain=(1.0*M_Q8);
		pSps->outputGain=(1.0*M_Q8);
		pChan->prxVoiceMeasure=pSps->sink;
		pChan->prxVoiceAdjust=&(pSps->outputGain);	
	}

	if(pChan->rxDelayLineEnable)
	{
		TRACEX(("create delayline\n"));
		pSps=pChan->spsDelayLine=pSps->nextSps=createPmrSps();
		pSps->sigProc=DelayLine;
		pSps->source=pChan->pRxSpeaker;	 
		pSps->sink=pChan->pRxSpeaker;
		pSps->enabled=0;
		pSps->inputGain=1*M_Q8;	
		pSps->outputGain=1*M_Q8;
		pSps->nSamples=pChan->nSamplesRx;
		pSps->buffSize=4096;
		pSps->buff=calloc(4096,2);	 		// one second maximum
		pSps->buffLead = (SAMPLE_RATE_NETWORK*0.100);
		pSps->buffOutIndex=0;
	}

	if(pChan->rxCdType==CD_XPMR_VOX)
	{
		TRACEX(("create vox measureblock\n"));
		pSps=pChan->spsRxVox=pSps->nextSps=createPmrSps();
		pSps->sigProc=MeasureBlock;
		pSps->parentChan=pChan;
		pSps->source=pChan->pRxBase;
		pSps->sink=pChan->prxDebug1;
		pSps->inputGain=1*M_Q8;	
		pSps->outputGain=1*M_Q8;
		pSps->nSamples=pChan->nSamplesRx;
		pSps->discfactor=3;
		pSps->setpt=(0.01*M_Q15);
		pSps->hyst=(pSps->setpt/10);
		pSps->enabled=1;
	}

	// tuning measure block
	pSps=pChan->spsMeasure=pSps->nextSps=createPmrSps();
	pSps->parentChan=pChan;
	pSps->source=pChan->spsRx->sink;					 
	pSps->sink=pChan->prxDebug2;
	pSps->sigProc=MeasureBlock;
	pSps->enabled=0;
	pSps->nSamples=pChan->nSamplesRx;
	pSps->discfactor=10;	    

	pSps->nextSps=NULL;		// last sps in chain RX


	// CREATE TRANSMIT CHAIN
	TRACEX((" create tx\n"));
	inputTmp=NULL;
	pSps = NULL;

	// allocate space for first sps and set pointers

	// Tx HPF SubAudible
	if(pChan->txHpfEnable)
	{
		pSps=createPmrSps();
		pChan->spsTx=pSps;
		pSps->source=pChan->pTxBase;
		pSps->sink=pChan->pTxHpf;  
		pSps->sigProc=pmr_gp_fir;
		pSps->enabled=1;
		pSps->numChanOut=1;
		pSps->selChanOut=0;
		pSps->nSamples=pChan->nSamplesTx;
		pSps->decimator=pSps->decimate=1;
		pSps->interpolate=1;
		pSps->ncoef=taps_fir_hpf_300_9_66;
		pSps->size_coef=2;
		pSps->coef=(void*)coef_fir_hpf_300_9_66;
		pSps->nx=taps_fir_hpf_300_9_66;
		pSps->size_x=2;
		pSps->x=(void*)(calloc(pSps->nx,pSps->size_x));
		if(pSps==NULL)printf("Error: calloc(), createPmrChannel()\n"); 
		pSps->calcAdjust=gain_fir_hpf_300_9_66;
		pSps->inputGain=(1*M_Q8);
		pSps->outputGain=(1*M_Q8);
		inputTmp=pChan->pTxHpf;
	}

	// Tx PreEmphasis
	if(pChan->txPreEmpEnable)
	{
		if(pSps==NULL) pSps=pChan->spsTx=createPmrSps();
		else pSps=pSps->nextSps=createPmrSps();
		
		pSps->parentChan=pChan;
		pSps->source=inputTmp;
		pSps->sink=pChan->pTxPreEmp;  
		
		pSps->sigProc=gp_diff;
		pSps->enabled=1;
		pSps->nSamples=pChan->nSamplesTx;
	
		pSps->ncoef=taps_int_hpf_4000_1_2;
		pSps->size_coef=2;
		pSps->coef=(void*)coef_int_hpf_4000_1_2;
	
		pSps->nx=taps_int_hpf_4000_1_2;
		pSps->size_x=2;
		pSps->x=(void*)(calloc(pSps->nx,pSps->size_x));
		if(pSps==NULL)printf("Error: calloc(), createPmrChannel()\n");
		pSps->outputGain=(1*M_Q8);
		pSps->calcAdjust=gain_int_hpf_4000_1_2;
		pSps->inputGain=(1*M_Q8);
		pSps->outputGain=(1*M_Q8);
		inputTmp=pSps->sink;
	}

	// Tx Limiter
	if(pChan->txLimiterEnable)
	{
		if(pSps==NULL) pSps=pChan->spsTx=createPmrSps();
		else pSps=pSps->nextSps=createPmrSps();
		pSps->source=inputTmp;
		pSps->sink=pChan->pTxLimiter;
		pSps->sigProc=SoftLimiter;
		pSps->enabled=1;
		pSps->nSamples=pChan->nSamplesTx;
		pSps->inputGain=(1*M_Q8);
		pSps->outputGain=(1*M_Q8);
		pSps->setpt=12000;
		inputTmp=pSps->sink;
	}

	// Composite Mix of Voice and LSD
	if((pChan->txMixA==TX_OUT_COMPOSITE)||(pChan->txMixB==TX_OUT_COMPOSITE))
	{
		if(pSps==NULL)
			pSps=pChan->spsTx=createPmrSps();
		else
			pSps=pSps->nextSps=createPmrSps();
		pSps->source=inputTmp;
		pSps->sourceB=pChan->pTxLsdLpf;		 //asdf ??? !!! maw pTxLsdLpf
		pSps->sink=pChan->pTxComposite;
		pSps->sigProc=pmrMixer;
		pSps->enabled=1;
		pSps->nSamples=pChan->nSamplesTx;
		pSps->inputGain=2*M_Q8;	
		pSps->inputGainB=1*M_Q8/8; 
		pSps->outputGain=1*M_Q8;
		pSps->setpt=0;
		inputTmp=pSps->sink;
		pChan->ptxCtcssAdjust=&pSps->inputGainB;
	}

	// Chan A Upsampler and Filter 
	if(pSps==NULL) pSps=pChan->spsTx=createPmrSps();
	else pSps=pSps->nextSps=createPmrSps();

	pChan->spsTxOutA=pSps;
	if(!pChan->spsTx)pChan->spsTx=pSps;
	pSps->parentChan=pChan;

	if(pChan->txMixA==TX_OUT_COMPOSITE)
	{
		pSps->source=pChan->pTxComposite;	
	}
	else if(pChan->txMixA==TX_OUT_LSD)
	{
		pSps->source=pChan->pTxLsdLpf;	
	}
	else if(pChan->txMixA==TX_OUT_VOICE)
	{
		pSps->source=pChan->pTxHpf;
	}
	else if (pChan->txMixA==TX_OUT_AUX)
	{
		pSps->source=inputTmp;				 
	}
	else
	{
		pSps->source=NULL;				 
	}
	
	pSps->sink=pChan->pTxOut;  
	pSps->sigProc=pmr_gp_fir;
	pSps->enabled=1;
	pSps->numChanOut=2;
	pSps->selChanOut=0;
	pSps->nSamples=pChan->nSamplesTx;
	pSps->interpolate=6;
	pSps->ncoef=taps_fir_lpf_3K_1;
	pSps->size_coef=2;
	pSps->coef=(void*)coef_fir_lpf_3K_1;
	pSps->nx=taps_fir_lpf_3K_1;
	pSps->size_x=2;
	pSps->x=(void*)(calloc(pSps->nx,pSps->size_x));
	if(pSps==NULL)printf("Error: calloc(), createPmrChannel()\n"); 
	pSps->calcAdjust=gain_fir_lpf_3K_1;
	pSps->inputGain=(1*M_Q8);
	pSps->outputGain=(1*M_Q8);
	if(pChan->txMixA==pChan->txMixB)pSps->monoOut=1;
	else pSps->monoOut=0;


	// Chan B Upsampler and Filter 
	if((pChan->txMixA!=pChan->txMixB)&&(pChan->txMixB!=TX_OUT_OFF))
	{
		if(pSps==NULL) pSps=pChan->spsTx=createPmrSps();
		else pSps=pSps->nextSps=createPmrSps();

		pChan->spsTxOutB=pSps;
		pSps->parentChan=pChan;
		if(pChan->txMixB==TX_OUT_COMPOSITE)
		{
			pSps->source=pChan->pTxComposite;	
		}
		else if(pChan->txMixB==TX_OUT_LSD)
		{
			pSps->source=pChan->pTxLsdLpf;
			// pChan->ptxCtcssAdjust=&pSps->inputGain;
		}
		else if(pChan->txMixB==TX_OUT_VOICE)
		{
			pSps->source=inputTmp;
		}
		else if(pChan->txMixB==TX_OUT_AUX)
		{
			pSps->source=pChan->pTxHpf;
		}
		else
		{
			pSps->source=NULL;	 
		}

		pSps->sink=pChan->pTxOut;  
		pSps->sigProc=pmr_gp_fir;
		pSps->enabled=1;
		pSps->numChanOut=2;
		pSps->selChanOut=1;
		pSps->mixOut=0;
		pSps->nSamples=pChan->nSamplesTx;
		pSps->interpolate=6;
		pSps->ncoef=taps_fir_lpf_3K_1;
		pSps->size_coef=2;
		pSps->coef=(void*)coef_fir_lpf_3K_1;
		pSps->nx=taps_fir_lpf_3K_1;
		pSps->size_x=2;
		pSps->x=(void*)(calloc(pSps->nx,pSps->size_x));
		if(pSps==NULL)printf("Error: calloc(), createPmrChannel()\n");
		pSps->calcAdjust=(gain_fir_lpf_3K_1);
		pSps->inputGain=(1*M_Q8);
		pSps->outputGain=(1*M_Q8);
		
	}

	pSps->nextSps=NULL;

	#if XPMR_DEBUG0 == 1
	{
	    t_tdet     	*ptdet;
		TRACEX((" configure tracing\n"));
		
		pChan->rxCtcss->pDebug0=calloc(numSamples,2);
		pChan->rxCtcss->pDebug1=calloc(numSamples,2);
		pChan->rxCtcss->pDebug2=calloc(numSamples,2);

		for(i=0;i<CTCSS_NUM_CODES;i++){
			ptdet=&(pChan->rxCtcss->tdet[i]);
			ptdet->pDebug0=calloc(numSamples,2);
			ptdet->pDebug1=calloc(numSamples,2);
			ptdet->pDebug2=calloc(numSamples,2);
		}

		// buffer, 2 bytes per sample, and 16 channels
		pChan->prxDebug=calloc(numSamples*16,2);
		pChan->ptxDebug=calloc(numSamples*16,2);
	}
 	#endif

	TRACEX((" createPmrChannel() end\n"));

	return pChan;
}
/*	
*/
i16 destroyPmrChannel(t_pmr_chan *pChan)
{
	t_pmr_sps  	*pmr_sps, *tmp_sps;
	i16 i;

	TRACEX(("destroyPmrChannel()\n"));

	free(pChan->pRxDemod);
	free(pChan->pRxNoise);
	free(pChan->pRxBase);
	free(pChan->pRxHpf);
	free(pChan->pRxLsd);
	free(pChan->pRxSpeaker);
	free(pChan->pRxDcTrack);
	if(pChan->pRxLsdLimit)free(pChan->pRxLsdLimit);
	free(pChan->pTxBase);
	free(pChan->pTxHpf);
	free(pChan->pTxPreEmp);
	free(pChan->pTxLimiter);
	free(pChan->pTxLsd);
	free(pChan->pTxLsdLpf);
	if(pChan->pTxComposite)free(pChan->pTxComposite);
	free(pChan->pTxCode);
	free(pChan->pTxOut);

	if(pChan->pSigGen0)free(pChan->pSigGen0);
	if(pChan->pSigGen1)free(pChan->pSigGen1);

	#if XPMR_DEBUG0 == 1
	free(pChan->pTxPttIn);
	free(pChan->pTxPttOut);
	if(pChan->prxDebug)free(pChan->prxDebug);
	if(pChan->ptxDebug)free(pChan->ptxDebug);
	free(pChan->rxCtcss->pDebug0);
	free(pChan->rxCtcss->pDebug1);

	free(pChan->prxDebug0);
 	free(pChan->prxDebug1);
	free(pChan->prxDebug2);
	free(pChan->prxDebug3);

	free(pChan->ptxDebug0);
 	free(pChan->ptxDebug1);
	free(pChan->ptxDebug2);
	free(pChan->ptxDebug3);

	for(i=0;i<CTCSS_NUM_CODES;i++)
	{
		free(pChan->rxCtcss->tdet[i].pDebug0);
		free(pChan->rxCtcss->tdet[i].pDebug1);
		free(pChan->rxCtcss->tdet[i].pDebug2);
	}
	#endif

	free(pChan->pRxCtcss);

	pmr_sps=pChan->spsRx;
							
	while(pmr_sps)
	{
		tmp_sps = pmr_sps;
		pmr_sps = tmp_sps->nextSps;
		destroyPmrSps(tmp_sps);
	}

	free(pChan);

	return 0;
}
/*
*/
t_pmr_sps *createPmrSps(void)
{
	t_pmr_sps  *pSps;

	TRACEX(("createPmrSps()\n"));

	pSps = (t_pmr_sps *)calloc(sizeof(t_pmr_sps),1);

	if(!pSps)printf("Error: createPmrSps()\n");

	// pSps->x=calloc(pSps->nx,pSps->size_x);

	return pSps;
}
/*
*/
i16 destroyPmrSps(t_pmr_sps  *pSps)
{
	TRACEX(("destroyPmrSps(%i)\n",pSps->index));

	if(pSps->x!=NULL)free(pSps->x);
	free(pSps);
	return 0;
}
/*	
	PmrRx does the whole buffer
*/
i16 PmrRx(t_pmr_chan *pChan, i16 *input, i16 *output)
{
	int i,ii;
	t_pmr_sps *pmr_sps;

	TRACEX(("PmrRx() %i\n",pChan->frameCountRx));

	if(pChan==NULL){
		printf("PmrRx() pChan == NULL\n");
		return 1;
	}

	pChan->frameCountRx++;

	pmr_sps=pChan->spsRx;		// first sps
	pmr_sps->source=input;

	if(output!=NULL)pChan->spsRxOut->sink=output;	 //last sps

	#if 0
	if(pChan->inputBlanking>0)
	{
		pChan->inputBlanking-=pChan->nSamplesRx;
		if(pChan->inputBlanking<0)pChan->inputBlanking=0;
		for(i=0;i<pChan->nSamplesRx*6;i++)
			input[i]=0;
	}
	#endif
	
	// || (pChan->radioDuplex && (pChan->pttIn || pChan->pttOut)))
	if(pChan->rxCpuSaver && !pChan->rxCarrierDetect) 
	{
		if(pChan->spsRxHpf)pChan->spsRxHpf->enabled=0;
		if(pChan->spsRxDeEmp)pChan->spsRxDeEmp->enabled=0;
	}
	else
	{
		if(pChan->spsRxHpf)pChan->spsRxHpf->enabled=1;
		if(pChan->spsRxDeEmp)pChan->spsRxDeEmp->enabled=1;
	}

	i=0;
	while(pmr_sps!=NULL && pmr_sps!=0)
	{
		TRACEX(("PmrRx() sps %i\n",i++));
		pmr_sps->sigProc(pmr_sps);
		pmr_sps = (t_pmr_sps *)(pmr_sps->nextSps);
		//pmr_sps=NULL;	// sph maw
	}

	#define XPMR_VOX_HANGTIME	2000
	
	if(pChan->rxCdType==CD_XPMR_VOX)
	{
		if(pChan->spsRxVox->compOut)
		{
			pChan->rxVoxTimer=XPMR_VOX_HANGTIME;    //VOX HangTime in ms
		}
		if(pChan->rxVoxTimer>0)
		{
			pChan->rxVoxTimer-=MS_PER_FRAME;
			pChan->rxCarrierDetect=1;
		}
		else
		{
			pChan->rxVoxTimer=0;
			pChan->rxCarrierDetect=0;
		}
	}
	else
	{
		pChan->rxCarrierDetect=!pChan->spsRx->compOut;
	}

	if( !pChan->rxCpuSaver || pChan->rxCarrierDetect 
		|| pChan->rxCtcss->decode!=-1) ctcss_detect(pChan);

	#if XPMR_DEBUG0 == 1
	// TRACEX(("Write file.\n"));
	ii=0;
	if(pChan->b.rxCapture)
	{
		for(i=0;i<pChan->nSamplesRx;i++)
		{
			pChan->prxDebug[ii++]=input[i*2*6];		  												// input data
			pChan->prxDebug[ii++]=output[i];														// output data
			pChan->prxDebug[ii++]=pChan->rxCarrierDetect*M_Q14;		 								// carrier detect
			if(pChan->rxCtcss)
				pChan->prxDebug[ii++]=pChan->rxCtcss->decode*M_Q15/CTCSS_NUM_CODES;					// decoded ctcss
			else
				pChan->prxDebug[ii++]=0;															
	
			pChan->prxDebug[ii++]=pChan->pRxNoise[i];												// rssi
			pChan->prxDebug[ii++]=pChan->pRxBase[i];												// decimated, low pass filtered
			pChan->prxDebug[ii++]=pChan->pRxHpf[i];													// output to network
			pChan->prxDebug[ii++]=pChan->pRxSpeaker[i];
	
			pChan->prxDebug[ii++]=pChan->pRxLsd[i];		  											// CTCSS Filtered
			pChan->prxDebug[ii++]=pChan->pRxDcTrack[i];												// DC Restoration
			pChan->prxDebug[ii++]=pChan->pRxLsdLimit[i];											// Amplitude Limited
			
			//pChan->prxDebug[ii++]=pChan->rxCtcss->tdet[pChan->rxCtcss->testIndex+1].pDebug0[i];	// Upper Adjacent CTCSS Code
			pChan->prxDebug[ii++]=pChan->rxCtcss->tdet[pChan->rxCtcss->testIndex].pDebug0[i];		// Primary CTCSS Code
			pChan->prxDebug[ii++]=pChan->rxCtcss->tdet[pChan->rxCtcss->testIndex].pDebug1[i];		// dv/dt of decoder output
			pChan->prxDebug[ii++]=pChan->rxCtcss->tdet[pChan->rxCtcss->testIndex].pDebug2[i];

			//pChan->prxDebug[ii++]=pChan->rxCtcss->tdet[pChan->rxCtcss->testIndex-1].pDebug0[i];	   	// Lower Adjacent CTCSS Code
			
			pChan->prxDebug[ii++]=pChan->prxDebug1[i];		// Measure Output for VOX
			pChan->prxDebug[ii++]=pChan->prxDebug2[i];	  	// Measure Output for Tuning
		}
	}
	#endif

	return 0;
}
/*	
	PmrTx does the whole buffer
*/
i16 PmrTx(t_pmr_chan *pChan, i16 *input, i16 *output)
{
	int i, hit=0;
	t_pmr_sps *pmr_sps;

	pChan->frameCountTx++;

	TRACEX(("PmrTx() %i\n",pChan->frameCountTx));

	if(pChan==NULL){
		printf("PmrTx() pChan == NULL\n");
		return 1;
	}

	if(pChan->b.startSpecialTone)
	{
		pChan->b.startSpecialTone=0;
		pChan->spsSigGen1->option=1;
		pChan->spsSigGen1->enabled=1;
		pChan->b.doingSpecialTone=1;
	} else if(pChan->b.stopSpecialTone)
	{
		pChan->b.stopSpecialTone=0;
		pChan->spsSigGen1->option=0;
		pChan->b.doingSpecialTone=0;
		pChan->spsSigGen1->enabled=0;
	} else if(pChan->b.doingSpecialTone)
	{
		pChan->spsSigGen1->sink=output;
		pChan->spsSigGen1->sigProc(pChan->spsSigGen1);
		for(i=0;i<(pChan->nSamplesTx*2*6);i+=2)output[i+1]=output[i];
		return 0;
	}

	// handle transmitter ptt input
	hit=0;
	if( pChan->txPttIn && pChan->txState==0)
	{
		pChan->txState = 2;
		pChan->txPttOut=1;
		pChan->spsSigGen0->freq=pChan->txCtcssFreq*10;
		pChan->spsSigGen0->option=1;
		pChan->spsSigGen0->enabled=1;
		if(pChan->spsTxOutA)pChan->spsTxOutA->enabled=1;
		if(pChan->spsTxOutB)pChan->spsTxOutB->enabled=1;
		if(pChan->spsTxLsdLpf)pChan->spsTxLsdLpf->enabled=1;
		TRACEX((" TxOn\n"));
	}
	else if(!pChan->txPttIn && pChan->txState==2)
	{
		if( pChan->txTocType==TOC_NONE || !pChan->txCtcssFreq )
		{
			hit=1;
			TRACEX((" Tx Off Immediate.\n"));
        }
		else if(pChan->txCtcssFreq && pChan->txTocType==TOC_NOTONE)
		{
			pChan->txState=3;
			pChan->txHangTime=TOC_NOTONE_TIME/MS_PER_FRAME;
			pChan->spsSigGen0->option=3;
			TRACEX((" Tx Turn Off No Tone Start.\n"));
		}
 		else
		{
			pChan->txState=3;
			pChan->txHangTime=0;
			pChan->spsSigGen0->option=2;
			TRACEX((" Tx Turn Off Phase Shift Start.\n"));
		}
	} 
	else if(pChan->txState==3)
	{
		if(pChan->txHangTime)
		{
			if(--pChan->txHangTime==0)hit=1;

		}
		else if(pChan->txHangTime<=0 && pChan->spsSigGen0->state==0)
		{	
			hit=1;
			TRACEX((" Tx Off TOC.\n"));
		}
		if(pChan->txPttIn)
		{
			TRACEX((" Tx Key During HangTime\n"));		 
			if((pChan->txTocType==TOC_PHASE)||(pChan->txTocType==TOC_NONE))
			{
				pChan->txState = 2;
				hit=0;
			}
		}
	}

	if( pChan->txCpuSaver && !hit && !pChan->txPttIn && !pChan->txPttOut && pChan->txState==0 ) return (1); 

	if(hit)
	{
		pChan->txPttOut=0;
		pChan->txState=0;
		if(pChan->spsTxLsdLpf)pChan->spsTxLsdLpf->option=3;
		if(pChan->spsTxOutA)pChan->spsTxOutA->option=3;
		if(pChan->spsTxOutB)pChan->spsTxOutB->option=3;
		TRACEX((" Tx Off hit.\n"));
	}

	if(pChan->spsSigGen0)
	{
		pChan->spsSigGen0->sigProc(pChan->spsSigGen0);
		pmr_sps=pChan->spsSigGen0->nextSps;
		i=0;
		while(pmr_sps!=NULL && pmr_sps!=0)
		{
			TRACEX((" PmrTx() subaudible sps %i\n",i++));
			//printf(" CTCSS ENCODE %i %i\n",pChan->spsSigGen0->freq,pChan->spsSigGen0->outputGain);
			pmr_sps->sigProc(pmr_sps);
			pmr_sps = (t_pmr_sps *)(pmr_sps->nextSps);
		}
	}

	if(pChan->spsSigGen1 && pChan->spsSigGen1->enabled)
	{
		pChan->spsSigGen1->sigProc(pChan->spsSigGen1);
	}

	// Do Voice
	pmr_sps=pChan->spsTx;
	if(!pChan->spsSigGen1->enabled)pmr_sps->source=input;
	else input=pmr_sps->source;

	if(output!=NULL)
	{
		if(pChan->spsTxOutA)pChan->spsTxOutA->sink=output;
		if(pChan->spsTxOutB)pChan->spsTxOutB->sink=output;
	}

	i=0;
	while(pmr_sps!=NULL && pmr_sps!=0)
	{
		TRACEX((" PmrTx() sps %i\n",i++));
		pmr_sps->sigProc(pmr_sps);
		pmr_sps = (t_pmr_sps *)(pmr_sps->nextSps);
	}


	if(pChan->txMixA==TX_OUT_OFF || !pChan->txPttOut){
		for(i=0;i<pChan->nSamplesTx*2*6;i+=2)output[i]=0;
	}

	if(pChan->txMixB==TX_OUT_OFF || !pChan->txPttOut ){
		for(i=0;i<pChan->nSamplesTx*2*6;i+=2)output[i+1]=0;
	}

	#if XPMR_DEBUG0 == 1
	if(pChan->b.txCapture)
	{
		i16 ii=0;
		for(i=0;i<pChan->nSamplesTx;i++)
		{
			pChan->ptxDebug[ii++]=input[i];
			pChan->ptxDebug[ii++]=output[i*2*6];
			pChan->ptxDebug[ii++]=output[(i*2*6)+1];
			pChan->ptxDebug[ii++]=pChan->txPttIn*8192;
	
			pChan->ptxDebug[ii++]=pChan->txPttOut*8192;
			if(pChan->txHpfEnable)pChan->ptxDebug[ii++]=pChan->pTxHpf[i];
			else pChan->ptxDebug[ii++]=0; 
			if(pChan->txPreEmpEnable)pChan->ptxDebug[ii++]=pChan->pTxPreEmp[i];
			else pChan->ptxDebug[ii++]=0;
			if(pChan->txLimiterEnable)pChan->ptxDebug[ii++]=pChan->pTxLimiter[i];
			else pChan->ptxDebug[ii++]=0;
	
			pChan->ptxDebug[ii++]=pChan->pTxLsd[i];
			pChan->ptxDebug[ii++]=pChan->pTxLsdLpf[i];
			pChan->ptxDebug[ii++]=pChan->pTxComposite[i];
			pChan->ptxDebug[ii++]=pChan->pSigGen1[i];
					 
			#if 1
			pChan->ptxDebug[ii++]=pChan->ptxDebug0[i];
			pChan->ptxDebug[ii++]=pChan->ptxDebug1[i];
			pChan->ptxDebug[ii++]=pChan->ptxDebug2[i];
			pChan->ptxDebug[ii++]=pChan->ptxDebug3[i];
			#else
			pChan->ptxDebug[ii++]=0;
			pChan->ptxDebug[ii++]=0;
			pChan->ptxDebug[ii++]=0;
			pChan->ptxDebug[ii++]=0;
			#endif
		}
	}
	#endif

	return 0;
}
/* end of file */
