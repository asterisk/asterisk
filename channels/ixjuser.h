/******************************************************************************
$Id$
$Log$
Revision 1.6  1999/12/01 05:25:58  markster
Version 0.1.5 from FTP

Revision 1.1  1999/12/01 05:25:58  markster
Start on the Internet Phone Jack channel

Revision 3.0  1999/11/18 22:21:43  root
Added isapnp kernel module support.

Revision 2.5  1999/11/09 15:42:06  root
Fixed convert code for older PhoneJACK cards.
Fixed G723.1 5.3 record bug.

Revision 2.4  1999/11/02 20:17:20  root
Fixed hookswitch bug for PhoneJACK
Added filter exception bit code.

Revision 2.3  1999/11/01 23:22:02  root
Added tone cadence generation.
Fixed hookswitch detection when PORT != POTS
Fixed ringing when PORT != POTS

Revision 2.2  1999/10/13 05:33:31  root
Added G.729A/B codec.
Fixed G.728 codec.

Revision 2.1  1999/10/11 19:29:46  root
Enhanced PSTN ring detection
Added Caller ID recognition
Added wink detection

Revision 2.0  1999/09/18 03:29:15  root
Added PSTN Support
Enhanced mixer functions
Fixed LineJACK and PhoneJACK Lite not reporting second IO range in p/proc/ioports
Added DSP Volume controls

Revision 1.23  1999/08/16 16:37:27  root
Added drybuffer ioctls
Added card type ioctl

Revision 1.22  1999/08/14 05:15:58  root
Fixed select timeout problems.  Was using the wrong wait queues.

Revision 1.21  1999/08/04 22:53:43  root
Added levels to AEC

Revision 1.20  1999/07/28 20:32:23  root
Added non-blocking rings
Added ring cadence

Revision 1.19  1999/07/14 21:08:10  root
fixed rcsid

Revision 1.18  1999/07/14 21:03:33  root
added rcsid

Revision 1.17  1999/07/12 20:20:00  root
Continuing work on 20ms and 10ms linear modes.

Revision 1.16  1999/07/09 19:13:50  root
Added IXJCTL_RATE to set the rate the DSP is polled.
Added 20ms and 10ms linear modes for LineJack and PhoneJack Lite.

Revision 1.15  1999/07/08 02:21:20  root
Async Notification on hookswitch changes.
Added IXJ_EXCEPTION bitfield structure for select exception sets
Added IXJCTL_DTMFASCII ioctl

Revision 1.14  1999/07/06 23:20:52  root
*** empty log message ***

Revision 1.14  1999/07/06 21:48:38  root
Fixed lockups in linear modes during write.

Revision 1.13  1999/06/28 18:30:44  root
Added buffer depth commands to reduce latency in compressed modes.

Revision 1.12  1999/06/25 21:03:18  root
Close device properly.
Started Mixer code for LineJack

Revision 1.11  1999/06/24 21:51:21  root
bug fixes

Revision 1.10  1999/06/17 18:23:16  root
Added tone generation.
Fixed LineJack hookswitch again.

Revision 1.9  1999/06/09 14:18:15  root
Fixed duplicate IOCTL numbers

Revision 1.8  1999/06/09 14:13:29  root
Added DTMF Recognition with Async Notification

Revision 1.7  1999/06/03 16:50:43  root
Added IXJCTL_HZ to allow changing the HZ value the driver uses.

Revision 1.6  1999/05/31 17:56:07  root
All linear modes now work properly.

Revision 1.5  1999/05/25 14:05:44  root
Added multiple codec selections.

Revision 1.4  1999/04/28 03:43:52  root
More work on echo cancellation

Revision 1.3  1999/04/27 17:17:12  root
added AEC

Revision 1.2  1999/04/22 11:43:52  root
Fixed stability problems

Revision 1.1  1999/03/25 21:52:42  root
Initial revision

*
* IOCTL's used for the Quicknet Cards
*
* If you use the IXJCTL_TESTRAM command, the card must be power cycled to
* reset the SRAM values before futher use.
*
******************************************************************************/
static char ixjuser_h_rcsid[] = "$Id$";

#define IXJCTL_DSP_RESET 		_IO  ('q', 0x80)
#define IXJCTL_RING                     _IO  ('q', 0x82)
#define IXJCTL_HOOKSTATE                _IO  ('q', 0x83)
#define IXJCTL_MAXRINGS			_IOW ('q', 0x84, char)
#define IXJCTL_RING_CADENCE		_IOW ('q', 0x85, short)
#define IXJCTL_RING_START		_IO  ('q', 0x86)
#define IXJCTL_RING_STOP		_IO  ('q', 0x87)
#define IXJCTL_CARDTYPE			_IOR ('q', 0x88, int)
#define IXJCTL_DSP_TYPE                 _IOR ('q', 0x8C, int)
#define IXJCTL_DSP_VERSION              _IOR ('q', 0x8D, int)
#define IXJCTL_DSP_IDLE			_IO  ('q', 0x8E)
#define IXJCTL_TESTRAM			_IO  ('q', 0x8F)

/******************************************************************************
*
* This group of IOCTLs deal with the record settings of the DSP
*
* The IXJCTL_REC_DEPTH command sets the internal buffer depth of the DSP.
* Setting a lower depth reduces latency, but increases the demand of the
* application to service the driver without frame loss.  The DSP has 480
* bytes of physical buffer memory for the record channel so the true
* maximum limit is determined by how many frames will fit in the buffer.
*
* 1 uncompressed (480 byte) 16-bit linear frame.
* 2 uncompressed (240 byte) 8-bit A-law/mu-law frames.
* 15 TrueSpeech 8.5 frames.
* 20 TrueSpeech 6.3,5.3,4.8 or 4.1 frames.
*
* The default in the driver is currently set to 2 frames.
*
* The IXJCTL_REC_VOLUME and IXJCTL_PLAY_VOLUME commands both use a Q8
* number as a parameter, 0x100 scales the signal by 1.0, 0x200 scales the
* signal by 2.0, 0x80 scales the signal by 0.5.  No protection is given
* against over-scaling, if the multiplication factor times the input
* signal exceeds 16 bits, overflow distortion will occur.  The default
* setting is 0x100 (1.0).
*
* The IXJCTL_REC_LEVEL returns the average signal level (not r.m.s.) on
* the most recently recorded frame as a 16 bit value.
******************************************************************************/

#define IXJCTL_REC_CODEC                _IOW ('q', 0x90, int)
#define IXJCTL_REC_START                _IO  ('q', 0x92)
#define IXJCTL_REC_STOP                 _IO  ('q', 0x93)
#define IXJCTL_REC_DEPTH		_IOW ('q', 0x94, int)
#define IXJCTL_FRAME			_IOW ('q', 0x95, int)
#define IXJCTL_REC_VOLUME		_IOW ('q', 0x96, int)
#define IXJCTL_REC_LEVEL		_IO  ('q', 0x97)

typedef enum{
f300_640 = 4, f300_500, f1100, f350, f400, f480, f440, f620, f20_50,
f133_200, f300, f300_420, f330, f300_425, f330_440, f340, f350_400,
f350_440, f350_450, f360, f380_420, f392, f400_425, f400_440, f400_450,
f420, f425, f425_450, f425_475, f435, f440_450, f440_480, f445, f450,
f452, f475, f480_620, f494, f500, f520, f523, f525, f540_660, f587,
f590, f600, f660, f700, f740, f750, f750_1450, f770, f800, f816, f850,
f857_1645, f900, f900_1300, f935_1215, f941_1477, f942, f950, f950_1400,
f975, f1000, f1020, f1050, f1100_1750, f1140, f1200, f1209, f1330, f1336,
lf1366, f1380, f1400, f1477, f1600, f1633_1638, f1800, f1860
}IXJ_FILTER_FREQ;

typedef struct
{
  unsigned int filter;
  IXJ_FILTER_FREQ freq;
  char enable;
}IXJ_FILTER;

#define IXJCTL_SET_FILTER		_IOW ('q', 0x98, IXJ_FILTER *)
#define IXJCTL_GET_FILTER_HIST		_IOW ('q', 0x9B, int)
/******************************************************************************
*
* This IOCTL allows you to reassign values in the tone index table.  The
* tone table has 32 entries (0 - 31), but the driver only allows entries
* 13 - 27 to be modified, entry 0 is reserved for silence and 1 - 12 are
* the standard DTMF digits and 28 - 31 are the DTMF tones for A, B, C & D.
* The positions used internally for Call Progress Tones are as follows:
*    Dial Tone   - 25
*    Ring Back   - 26
*    Busy Signal - 27
*
* The freq values are calculated as:
* freq = cos(2 * PI * frequency / 8000)
*
* The most commonly needed values are already calculated and listed in the
* enum IXJ_TONE_FREQ.  Each tone index can have two frequencies with
* different gains, if you are only using a single frequency set the unused
* one to 0.
*
* The gain values range from 0 to 15 indicating +6dB to -24dB in 2dB
* increments.
*
******************************************************************************/

typedef enum
{
hz20 = 0x7ffa, 
hz50 = 0x7fe5, 
hz133 = 0x7f4c, 
hz200 = 0x7e6b, 
hz261 = 0x7d50, /* .63 C1  */
hz277 = 0x7cfa, /* .18 CS1 */
hz293 = 0x7c9f, /* .66 D1  */
hz300 = 0x7c75, 
hz311 = 0x7c32, /* .13 DS1 */
hz329 = 0x7bbf, /* .63 E1  */
hz330 = 0x7bb8, 
hz340 = 0x7b75, 
hz349 = 0x7b37, /* .23 F1  */
hz350 = 0x7b30, 
hz360 = 0x7ae9, 
hz369 = 0x7aa8, /* .99 FS1 */
hz380 = 0x7a56, 
hz392 = 0x79fa, /* .00 G1  */
hz400 = 0x79bb, 
hz415 = 0x7941, /* .30 GS1 */
hz420 = 0x7918, 
hz425 = 0x78ee, 
hz435 = 0x7899, 
hz440 = 0x786d, /* .00 A1  */
hz445 = 0x7842, 
hz450 = 0x7815, 
hz452 = 0x7803, 
hz466 = 0x7784, /* .16 AS1 */
hz475 = 0x7731, 
hz480 = 0x7701, 
hz493 = 0x7685, /* .88 B1  */
hz494 = 0x767b, 
hz500 = 0x7640, 
hz520 = 0x7578, 
hz523 = 0x7559, /* .25 C2  */
hz525 = 0x7544, 
hz540 = 0x74a7, 
hz554 = 0x7411, /* .37 CS2 */
hz587 = 0x72a1, /* .33 D2  */
hz590 = 0x727f, 
hz600 = 0x720b, 
hz620 = 0x711e, 
hz622 = 0x7106, /* .25 DS2 */
hz659 = 0x6f3b, /* .26 E2  */
hz660 = 0x6f2e, 
hz698 = 0x6d3d, /* .46 F2  */
hz700 = 0x6d22, 
hz739 = 0x6b09, /* .99 FS2 */
hz740 = 0x6afa, 
hz750 = 0x6a6c, 
hz770 = 0x694b, 
hz783 = 0x688b, /* .99 G2  */
hz800 = 0x678d, 
hz816 = 0x6698, 
hz830 = 0x65bf, /* .61 GS2 */
hz850 = 0x6484, 
hz857 = 0x6414, 
hz880 = 0x629f, /* .00 A2  */
hz900 = 0x6154, 
hz932 = 0x5f35, /* .33 AS2 */
hz935 = 0x5f01, 
hz941 = 0x5e9a, 
hz942 = 0x5e88, 
hz950 = 0x5dfd, 
hz975 = 0x5c44, 
hz1000 = 0x5a81, 
hz1020 = 0x5912, 
hz1050 = 0x56e2, 
hz1100 = 0x5320, 
hz1140 = 0x5007, 
hz1200 = 0x4b3b, 
hz1209 = 0x4a80, 
hz1215 = 0x4a02, 
hz1250 = 0x471c, 
hz1300 = 0x42e0, 
hz1330 = 0x4049, 
hz1336 = 0x3fc4, 
hz1366 = 0x3d22, 
hz1380 = 0x3be4, 
hz1400 = 0x3a1b, 
hz1450 = 0x3596, 
hz1477 = 0x331c, 
hz1500 = 0x30fb, 
hz1600 = 0x278d, 
hz1633 = 0x2462, 
hz1638 = 0x23e7, 
hz1645 = 0x233a, 
hz1750 = 0x18f8, 
hz1800 = 0x1405, 
hz1860 = 0xe0b, 
hz2100 = 0xf5f6, 
hz2450 = 0xd3b3
}IXJ_FREQ;

typedef enum
{
C1  = hz261,
CS1 = hz277,
D1  = hz293,
DS1 = hz311,
E1  = hz329,
F1  = hz349,
FS1 = hz369,
G1  = hz392,
GS1 = hz415,
A1  = hz440,
AS1 = hz466,
B1  = hz493,
C2  = hz523,
CS2 = hz554,
D2  = hz587,
DS2 = hz622,
E2  = hz659,
F2  = hz698,
FS2 = hz739,
G2  = hz783,
GS2 = hz830,
A2  = hz880,
AS2 = hz932,
}IXJ_NOTE;

typedef struct
{
  int tone_index;
  int freq0;
  int gain0;
  int freq1;
  int gain1;
}IXJ_TONE;

#define IXJCTL_INIT_TONE		_IOW ('q', 0x99, IXJ_TONE *)

/******************************************************************************
*
* The IXJCTL_TONE_CADENCE ioctl defines tone sequences used for various
* Call Progress Tones (CPT).  This is accomplished by setting up an array of
* IXJ_CADENCE_ELEMENT structures that sequentially define the states of
* the tone sequence.  The tone_on_time and tone_off time are in
* 250 microsecond intervals.  A pointer to this array is passed to the
* driver as the ce element of an IXJ_CADENCE structure.  The elements_used
* must be set to the number of IXJ_CADENCE_ELEMENTS in the array.  The
* termination variable defines what to do at the end of a cadence, the
* options are to play the cadence once and stop, to repeat the last
* element of the cadence indefinatly, or to repeat the entire cadence
* indefinatly.  The ce variable is a pointer to the array of IXJ_TONE
* structures.  If the freq0 variable is non-zero, the tone table contents
* for the tone_index are updated to the frequencies and gains defined.  It
* should be noted that DTMF tones cannot be reassigned, so if DTMF tone
* table indexs are used in a cadence the frequency and gain variables will
* be ignored.
*
* If the array elements contain frequency parameters the driver will
* initialize the needed tone table elements and begin playing the tone,
* there is no preset limit on the number of elements in the cadence.  If
* there is more than one frequency used in the cadence, sequential elements
* of different frequencies MUST use different tone table indexes.  Only one
* cadence can be played at a time.  It is possible to build complex
* cadences with multiple frequencies using 2 tone table indexes by
* alternating between them.
*
******************************************************************************/

typedef struct
{
  int index;
  int tone_on_time;
  int tone_off_time;
  int freq0;
  int gain0;
  int freq1;
  int gain1;
}IXJ_CADENCE_ELEMENT;

typedef enum
{
  PLAY_ONCE,
  REPEAT_LAST_ELEMENT,
  REPEAT_ALL
}IXJ_CADENCE_TERM;

typedef struct
{
  int elements_used;
  IXJ_CADENCE_TERM termination;
  IXJ_CADENCE_ELEMENT *ce;
}IXJ_CADENCE;

#define IXJCTL_TONE_CADENCE			_IOW ('q', 0x9A, IXJ_CADENCE *)
/******************************************************************************
*
* This group of IOCTLs deal with the playback settings of the DSP
*
******************************************************************************/

#define IXJCTL_PLAY_CODEC               _IOW ('q', 0xA0, int)
#define IXJCTL_PLAY_START               _IO  ('q', 0xA2)
#define IXJCTL_PLAY_STOP                _IO  ('q', 0xA3)
#define IXJCTL_PLAY_DEPTH		_IOW ('q', 0xA4, int)
#define IXJCTL_PLAY_VOLUME		_IOW ('q', 0xA5, int)
#define IXJCTL_PLAY_LEVEL		_IO  ('q', 0xA6)

/******************************************************************************
*
* This group of IOCTLs deal with the Acoustic Echo Cancellation settings
* of the DSP
*
* Issueing the IXJCTL_AEC_START command with a value of AEC_OFF has the
* same effect as IXJCTL_AEC_STOP.  This is to simplify slider bar
* controls.
******************************************************************************/
#define IXJCTL_AEC_START		_IOW ('q', 0xB0, int)
#define IXJCTL_AEC_STOP			_IO  ('q', 0xB1)

#define AEC_OFF   0
#define AEC_LOW   1
#define AEC_MED   2
#define AEC_HIGH  3
/******************************************************************************
*
* Call Progress Tones, DTMF, etc.
* Tone on and off times are in 250 microsecond intervals so
* ioctl(ixj1, IXJCTL_SET_TONE_ON_TIME, 360);
* will set the tone on time of board ixj1 to 360 * 250us = 90ms
* the default values of tone on and off times is 840 or 210ms
******************************************************************************/

#define IXJCTL_DTMF_READY		_IOR ('q', 0xC0, int)
#define IXJCTL_GET_DTMF                 _IOR ('q', 0xC1, int)
#define IXJCTL_GET_DTMF_ASCII           _IOR ('q', 0xC2, int)
#define IXJCTL_EXCEPTION		_IOR ('q', 0xC4, int)
#define IXJCTL_PLAY_TONE		_IOW ('q', 0xC6, char)
#define IXJCTL_SET_TONE_ON_TIME		_IOW ('q', 0xC7, int)
#define IXJCTL_SET_TONE_OFF_TIME	_IOW ('q', 0xC8, int)
#define IXJCTL_GET_TONE_ON_TIME		_IO  ('q', 0xC9)
#define IXJCTL_GET_TONE_OFF_TIME	_IO  ('q', 0xCA)
#define IXJCTL_GET_TONE_STATE		_IO  ('q', 0xCB)
#define IXJCTL_BUSY			_IO  ('q', 0xCC)
#define IXJCTL_RINGBACK			_IO  ('q', 0xCD)
#define IXJCTL_DIALTONE			_IO  ('q', 0xCE)

// This IOCTL replaced both IXJCTL_BUSY_STOP and IXJCTL_RINGBACK_STOP and
// should be used from now on to stop all Call Progress Tones.  It will
// actually abort any tone, regardless of time left in the tone_on_time
// and tone_off_time counters.
#define IXJCTL_CPT_STOP			_IO  ('q', 0xCF)

/******************************************************************************
* LineJack specific IOCTLs
*
* The lsb 4 bits of the LED argument represent the state of each of the 4
* LED's on the LineJack
******************************************************************************/

#define IXJCTL_SET_LED			_IOW ('q', 0xD0, int)
#define IXJCTL_MIXER			_IOW ('q', 0xD1, int)

/******************************************************************************
* 
* The master volume controls use attenuation with 32 levels from 0 to -62dB
* with steps of 2dB each, the defines should be OR'ed together then sent
* as the parameter to the mixer command to change the mixer settings.
* 
******************************************************************************/
#define MIXER_MASTER_L		0x0100
#define MIXER_MASTER_R		0x0200
#define ATT00DB			0x00
#define ATT02DB			0x01
#define ATT04DB			0x02
#define ATT06DB			0x03
#define ATT08DB			0x04
#define ATT10DB			0x05
#define ATT12DB			0x06
#define ATT14DB			0x07
#define ATT16DB			0x08
#define ATT18DB			0x09
#define ATT20DB			0x0A
#define ATT22DB			0x0B
#define ATT24DB			0x0C
#define ATT26DB			0x0D
#define ATT28DB			0x0E
#define ATT30DB			0x0F
#define ATT32DB			0x10
#define ATT34DB			0x11
#define ATT36DB			0x12
#define ATT38DB			0x13
#define ATT40DB			0x14
#define ATT42DB			0x15
#define ATT44DB			0x16
#define ATT46DB			0x17
#define ATT48DB			0x18
#define ATT50DB			0x19
#define ATT52DB			0x1A
#define ATT54DB			0x1B
#define ATT56DB			0x1C
#define ATT58DB			0x1D
#define ATT60DB			0x1E
#define ATT62DB			0x1F
#define MASTER_MUTE		0x80

/******************************************************************************
* 
* The input volume controls use gain with 32 levels from +12dB to -50dB
* with steps of 2dB each, the defines should be OR'ed together then sent
* as the parameter to the mixer command to change the mixer settings.
* 
******************************************************************************/
#define MIXER_PORT_CD_L		0x0600
#define MIXER_PORT_CD_R		0x0700
#define MIXER_PORT_LINE_IN_L	0x0800
#define MIXER_PORT_LINE_IN_R	0x0900
#define MIXER_PORT_POTS_REC	0x0C00
#define MIXER_PORT_MIC		0x0E00

#define GAIN12DB		0x00
#define GAIN10DB		0x01
#define GAIN08DB		0x02
#define GAIN06DB		0x03
#define GAIN04DB		0x04
#define GAIN02DB		0x05
#define GAIN00DB		0x06
#define GAIN_02DB		0x07
#define GAIN_04DB		0x08
#define GAIN_06DB		0x09
#define GAIN_08DB		0x0A
#define GAIN_10DB		0x0B
#define GAIN_12DB		0x0C
#define GAIN_14DB		0x0D
#define GAIN_16DB		0x0E
#define GAIN_18DB		0x0F
#define GAIN_20DB		0x10
#define GAIN_22DB		0x11
#define GAIN_24DB		0x12
#define GAIN_26DB		0x13
#define GAIN_28DB		0x14
#define GAIN_30DB		0x15
#define GAIN_32DB		0x16
#define GAIN_34DB		0x17
#define GAIN_36DB		0x18
#define GAIN_38DB		0x19
#define GAIN_40DB		0x1A
#define GAIN_42DB		0x1B
#define GAIN_44DB		0x1C
#define GAIN_46DB		0x1D
#define GAIN_48DB		0x1E
#define GAIN_50DB		0x1F
#define INPUT_MUTE		0x80

/******************************************************************************
* 
* The POTS volume control use attenuation with 8 levels from 0dB to -28dB
* with steps of 4dB each, the defines should be OR'ed together then sent
* as the parameter to the mixer command to change the mixer settings.
* 
******************************************************************************/
#define MIXER_PORT_POTS_PLAY	0x0F00

#define POTS_ATT_00DB		0x00
#define POTS_ATT_04DB		0x01
#define POTS_ATT_08DB		0x02
#define POTS_ATT_12DB		0x03
#define POTS_ATT_16DB		0x04
#define POTS_ATT_20DB		0x05
#define POTS_ATT_24DB		0x06
#define POTS_ATT_28DB		0x07
#define POTS_MUTE		0x80

/******************************************************************************
* 
* The DAA controls the interface to the PSTN port.  The driver loads the
* US coefficients by default, so if you live in a different country you
* need to load the set for your countries phone system.
* 
******************************************************************************/
#define IXJCTL_DAA_COEFF_SET		_IOW ('q', 0xD2, int)

#define DAA_US 		1  //PITA 8kHz
#define DAA_UK 		2  //ISAR34 8kHz
#define DAA_FRANCE 	3  //
#define DAA_GERMANY	4
#define DAA_AUSTRALIA	5
#define DAA_JAPAN	6

/******************************************************************************
* 
* Use IXJCTL_PORT to set or query the port the card is set to.  If the
* argument is set to PORT_QUERY, the return value of the ioctl will
* indicate which port is currently in use, otherwise it will change the
* port.
* 
******************************************************************************/
#define IXJCTL_PORT			_IOW ('q', 0xD3, int)

#define PORT_QUERY	0
#define PORT_POTS	1
#define PORT_PSTN	2
#define PORT_SPEAKER	3
#define PORT_HANDSET	4

#define IXJCTL_PSTN_SET_STATE		_IOW ('q', 0xD4, int)
#define IXJCTL_PSTN_GET_STATE		_IO  ('q', 0xD5)

#define PSTN_ON_HOOK	0
#define PSTN_RINGING	1
#define PSTN_OFF_HOOK	2
#define PSTN_PULSE_DIAL	3

/******************************************************************************
* 
* The DAA Analog GAIN sets 2 parameters at one time, the recieve gain (AGRR), 
* and the transmit gain (AGX).  OR together the components and pass them
* as the parameter to IXJCTL_DAA_AGAIN.  The default setting is both at 0dB.
* 
******************************************************************************/
#define IXJCTL_DAA_AGAIN		_IOW ('q', 0xD6, int)

#define AGRR00DB	0x00  // Analog gain in recieve direction 0dB
#define AGRR3_5DB	0x10  // Analog gain in recieve direction 3.5dB
#define AGRR06DB	0x30  // Analog gain in recieve direction 6dB

#define AGX00DB		0x00  // Analog gain in transmit direction 0dB
#define AGX_6DB		0x04  // Analog gain in transmit direction -6dB
#define AGX3_5DB	0x08  // Analog gain in transmit direction 3.5dB
#define AGX_2_5B	0x0C  // Analog gain in transmit direction -2.5dB

#define IXJCTL_PSTN_LINETEST		_IO  ('q', 0xD7)

typedef struct
{
  char month[3];
  char day[3];
  char hour[3];
  char min[3];
  int numlen;
  char number[11];
  int namelen;
  char name[80];
}IXJ_CID;

#define IXJCTL_CID			_IOR ('q', 0xD8, IXJ_CID *)
/******************************************************************************
* 
* The wink duration is tunable with this ioctl.  The default wink duration  
* is 320ms.  You do not need to use this ioctl if you do not require a
* different wink duration.
* 
******************************************************************************/
#define IXJCTL_WINK_DURATION		_IOW ('q', 0xD9, int)

/******************************************************************************
* 
* This ioctl will connect the POTS port to the PSTN port on the LineJACK
* In order for this to work properly the port selection should be set to
* the PSTN port with IXJCTL_PORT prior to calling this ioctl.  This will
* enable conference calls between PSTN callers and network callers.
* Passing a 1 to this ioctl enables the POTS<->PSTN connection while
* passing a 0 turns it back off.
* 
******************************************************************************/
#define IXJCTL_POTS_PSTN		_IOW ('q', 0xDA, int)

/******************************************************************************
*
* IOCTLs added by request.
*
* IXJCTL_HZ sets the value your Linux kernel uses for HZ as defined in
*           /usr/include/asm/param.h, this determines the fundamental
*           frequency of the clock ticks on your Linux system.  The kernel
*           must be rebuilt if you change this value, also all modules you
*           use (except this one) must be recompiled.  The default value
*           is 100, and you only need to use this IOCTL if you use some
*           other value.
*
*
* IXJCTL_RATE sets the number of times per second that the driver polls
*             the DSP.  This value cannot be larger than HZ.  By
*             increasing both of these values, you may be able to reduce
*             latency because the max hang time that can exist between the
*             driver and the DSP will be reduced.
*
******************************************************************************/

#define IXJCTL_HZ                       _IOW ('q', 0xE0, int)
#define IXJCTL_RATE                     _IOW ('q', 0xE1, int)
#define IXJCTL_FRAMES_READ		_IOR ('q', 0xE2, unsigned long)
#define IXJCTL_FRAMES_WRITTEN		_IOR ('q', 0xE3, unsigned long)
#define IXJCTL_READ_WAIT		_IOR ('q', 0xE4, unsigned long)
#define IXJCTL_WRITE_WAIT		_IOR ('q', 0xE5, unsigned long)
#define IXJCTL_DRYBUFFER_READ		_IOR ('q', 0xE6, unsigned long)
#define IXJCTL_DRYBUFFER_CLEAR		_IO  ('q', 0xE7)

/******************************************************************************
*
* The defined CODECs that can be used with the IXJCTL_REC_CODEC and
* IXJ_PLAY_CODEC IOCTLs.
*
* PCM uLaw mode is a "pass through" mode.
*
* 16 bit linear data is signed integer (2's complement form), positive
* full scale is 7FFFH, negative full scale is 8000H, zero is 0000H
*
* 8 bit linear data is signed byte (2's complement form), positive
* full scale is 7FH, negative full scale is 80H, zero is 00H
*
* 8 bit linear WSS data is unsigned byte, positive full scale is FFH,
* negative full scale is 00H, zero is 80H.  This is the format used for
* 8-bit WAVE format data (Windows Sound System).  Data can be converted
* between the two 8-bit formats by simply inverting the ms bit. 
*
* G729 currently works most reliably with 10ms frames.  Use 20ms and 30ms
* at your own risk.  If you really need larger frame sizes you can
* concatenate multiple 10ms frames.
*
******************************************************************************/
typedef enum{		// Bytes per 30ms audio frame
G723_63	=	1,	// 24
G723_53	=	2, 	// 20
TS85	=	3,	// 32 Does not currently work on LineJack
TS48	=	4,	// 18
TS41	=	5,	// 16
G728	=	6,	// 96 LineJack only!
G729	=	7,	// 30 LineJack only!
ULAW	=	8,	// 240
ALAW	=	9,	// 240 not implemented DO NOT USE!
LINEAR16=	10,	// 480
LINEAR8	=	11,	// 240
WSS	=	12	// 240
}IXJ_CODEC;
/******************************************************************************
*
* The intercom IOCTL's short the output from one card to the input of the
* other and vice versa (actually done in the DSP read function).  It is only
* necessary to execute the IOCTL on one card, but it is necessary to have
* both devices open to be able to detect hook switch changes.  The record
* codec and rate of each card must match the playback codec and rate of
* the other card for this to work properly.
*
******************************************************************************/

#define IXJCTL_INTERCOM_START 		_IOW ('q', 0xFD, int)
#define IXJCTL_INTERCOM_STOP  		_IOW ('q', 0xFE, int) 

/******************************************************************************
*
* This IOCTL will decrement the module usage counter so you can force
* the module to unload after a program bombs.
*
******************************************************************************/

#define IXJCTL_MODRESET		        _IO  ('q', 0xFF)

/******************************************************************************
*
* Various Defines used for the Quicknet Cards
*
******************************************************************************/

#define SYNC_MODE_CODEC         0
#define SYNC_MODE_DATA          1
#define SYNC_MODE_POLL          2
#define SYNC_MODE_HOST          3

#define RECORD_SYNC_MODE        0x5100
#define PLAYBACK_SYNC_MODE      0x5200

#define USA_RING_CADENCE	0xC0C0

/******************************************************************************
*
* The exception structure allows us to multiplex multiple events onto the
* select() exception set.  If any of these flags are set select() will
* return with a positive indication on the exception set.  The dtmf_ready
* bit indicates if there is data waiting in the DTMF buffer.  The
* hookstate bit is set if there is a change in hookstate status, it does not
* indicate the current state of the hookswitch.  The pstn_ring bit
* indicates that the DAA on a LineJACK card has detected ring voltage on
* the PSTN port.  The caller_id bit indicates that caller_id data has been
* recieved and is available.  The pstn_wink bit indicates that the DAA on
* the LineJACK has recieved a wink from the telco switch.  The f0, f1, f2
* and f3 bits indicate that the filter has been triggered by detecting the
* frequency programmed into that filter.
*
******************************************************************************/

typedef struct
{
  unsigned int dtmf_ready:1;
  unsigned int hookstate:1;
  unsigned int pstn_ring:1;
  unsigned int caller_id:1;
  unsigned int pstn_wink:1;
  unsigned int f0:1;
  unsigned int f1:1;
  unsigned int f2:1;
  unsigned int f3:1;
  unsigned int reserved:23;
}IXJ_EXCEPT;

typedef union
{
  IXJ_EXCEPT bits;
  unsigned int bytes;
}IXJ_EXCEPTION;

