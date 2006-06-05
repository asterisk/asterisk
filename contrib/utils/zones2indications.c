/*
 * zones2indications: print libtonozone data as Asterisk indications.conf
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * Author: Tzafrir Cohen <tzafrir.cohen@xorcom.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <tonezone.h>
#include <unistd.h>

#define PROGRAM "zones2indication"

void print_tone_zone_sound(struct tone_zone *zone_data, const char* name, 
    int toneid) {
  int i;
  for (i=0; i<ZT_TONE_MAX; i++) {
    if (zone_data->tones[i].toneid == toneid){
      printf("%s = %s\n", name, zone_data->tones[i].data);
      break;
    }
  }
}

void print_indications(struct tone_zone *zone_data) {
  int i;
  
  printf (
    "[%s]\n"
    "; Source: libtonezone.\n"
    "description = %s\n"
    "\n",
    zone_data->country, zone_data->description
  );
  
  printf(  
    "ringcadence = "
  );
  for(i=0; ; i++) {
    if (zone_data->ringcadence[i] == 0)
      break;
    if (i != 0)
      putchar(',');
    printf("%d",zone_data->ringcadence[i]);
  }
  putchar('\n');
  
  print_tone_zone_sound(zone_data, "dial",        ZT_TONE_DIALTONE);
  print_tone_zone_sound(zone_data, "busy",        ZT_TONE_BUSY);
  print_tone_zone_sound(zone_data, "ring",        ZT_TONE_RINGTONE);
  print_tone_zone_sound(zone_data, "congestion",  ZT_TONE_CONGESTION);
  print_tone_zone_sound(zone_data, "callwaiting", ZT_TONE_CALLWAIT);
  print_tone_zone_sound(zone_data, "dialrecall",  ZT_TONE_DIALRECALL);
  print_tone_zone_sound(zone_data, "record",      ZT_TONE_RECORDTONE);
  print_tone_zone_sound(zone_data, "info",        ZT_TONE_INFO);
  print_tone_zone_sound(zone_data, "stutter",     ZT_TONE_STUTTER);
  printf("\n\n");
}

int print_zone_by_id(int zone_num) {
  struct tone_zone *zone_data = tone_zone_find_by_num(zone_num);

  if (zone_data == NULL)
    return 1;

  print_indications(zone_data);

  return 0;
}

int print_zone_by_country(char* country) {
  struct tone_zone *zone_data = tone_zone_find(country);

  if (zone_data == NULL)
    return 1;

  print_indications(zone_data);

  return 0;
}

int print_all() {
  int i;
  /* loop over all possible zones */
  for (i=0; ; i++) {
    if (print_zone_by_id(i))
      break;
  }
  return 0;
}

void usage() {
  fprintf(stderr,
      PROGRAM ": print libtonozone data as Asterisk indications.conf\n"
      "\n"
      "Usage:\n"
      "  " PROGRAM " -a         Print all countries\n"
      "  " PROGRAM " -c <code>  Select country by two-letter country code\n"
      "  " PROGRAM " -n <num>   Select country by its internal libtonezone number\n"
      "  " PROGRAM " -h         Print this text.\n"
  );
}

int main(int argc, char* argv[]){
  int country_code = -1;
  int opt_print_all = 0;
  int opt;
  char* endptr = NULL;
  
  while((opt = getopt(argc, argv, "ac:hn:")) != -1) {
    switch(opt) {
      case 'a':
        return print_all();
      case 'c':
        return print_zone_by_country(optarg);
      case 'h':
        usage();
        return 0;
      case 'n':
        printf("number is %s.\n", optarg);
        country_code = strtol(optarg, &endptr, 10);
        return print_zone_by_id(country_code);
        /* FIXME: what if this is not a number?
        if (endptr != NULL) {
          fprintf(stderr, "Error: Invalid country code %s, %d.\n",optarg, country_code);
          usage();
          exit(1);
        }
        */
        break;
    }
  }
  
  /* If we got here, the user selected no option */
  usage();
  return 2;
}
