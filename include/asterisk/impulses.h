#include "hrirs_left.h"
#include "hrirs_right.h"

#if IMPULSE_LEN_LEFT != IMPULSE_LEN_RIGHT
#error "Impluse size has to be equal in the hrir header files!"
#endif

#if IMPULSE_SIZE_LEFT != IMPULSE_SIZE_RIGHT
#error "Impluse length has to be equal in the hrir header files!"
#endif

#if IMPULSE_SIZE_LEFT != 181
#error "The conference is desgined for 180 individual places at the moment. If you want to change this please alter the positions array first"
#endif


#define IMPULSE_LEN IMPULSE_LEN_LEFT
#define IMPULSE_SIZE IMPULSE_SIZE_LEFT
#define IMPULSE_CHANNEL_LEFT 0
#define IMPULSE_CHANNEL_RIGHT 1
/* We only support 20 ms interval length at the moment. */
#define IMPULSE_FIXED_MIXING_INTERVAL 20

int positions[IMPULSE_SIZE] = {90,80,100,70,110,60,120,50,130,40,140,20,160,0,180,85,95,75,105,65,115,55,125,45,135,30,150,10,170,87,93,82,98,77,103,72,108,67,113,62,118,57,123,52,128,47,133,42,138,35,145,25,155,15,165,5,175,88,92,86,83,97,81,99,78,102,76,104,73,94,107,71,109,68,112,66,114,63,117,61,119,58,122,56,124,53,127,51,129,48,132,46,134,43,137,41,139,37,143,32,148,27,153,22,158,17,163,12,168,7,96,173,2,178,89,91,84,79,101,74,106,69,111,64,116,59,121,54,126,49,131,44,136,38,142,36,144,33,147,31,149,28,152,26,154,23,157,21,159,162,16,164,13,167,11,169,8,172,6,174,3,177,1,179,39,141,34,146,29,151,24,156,19,161,14,166,9,171,4,18,176};
