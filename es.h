#include "language.h"

#define NPARAMS 3

typedef struct client CLIENT;
struct client
{
  int fd;
  int param[NPARAMS];
  char punct[128];
};


typedef struct setting setting;
struct setting
{
  char *name;
  char *value;
  setting *next;
};


int sockopen(char *fname);
int sockconnect(char *fname);
int speaker_tone(int freq, int dur);
int dsp_tone(int freq, int dur);
