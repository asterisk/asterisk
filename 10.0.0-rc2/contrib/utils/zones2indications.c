/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Tzafrir Cohen <tzafrir.cohen@xorcom.com>
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
 * \brief print libtonozone data as Asterisk indications.conf
 */ 

#include <stdio.h>
#include <stdlib.h>
#include <dahdi/tonezone.h>
#include <unistd.h>

#define PROGRAM "zones2indication"

void print_tone_zone_sound(struct ind_tone_zone *zone_data, const char* name, 
    int toneid) {
  int i;
  for (i=0; i<DAHDI_TONE_MAX; i++) {
    if (zone_data->tones[i].toneid == toneid){
      printf("%s = %s\n", name, zone_data->tones[i].data);
      break;
    }
  }
}

void print_indications(struct ind_tone_zone *zone_data) {
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
  
  print_tone_zone_sound(zone_data, "dial",        DAHDI_TONE_DIALTONE);
  print_tone_zone_sound(zone_data, "busy",        DAHDI_TONE_BUSY);
  print_tone_zone_sound(zone_data, "ring",        DAHDI_TONE_RINGTONE);
  print_tone_zone_sound(zone_data, "congestion",  DAHDI_TONE_CONGESTION);
  print_tone_zone_sound(zone_data, "callwaiting", DAHDI_TONE_CALLWAIT);
  print_tone_zone_sound(zone_data, "dialrecall",  DAHDI_TONE_DIALRECALL);
  print_tone_zone_sound(zone_data, "record",      DAHDI_TONE_RECORDTONE);
  print_tone_zone_sound(zone_data, "info",        DAHDI_TONE_INFO);
  print_tone_zone_sound(zone_data, "stutter",     DAHDI_TONE_STUTTER);
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
