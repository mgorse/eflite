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

/* es.c */
void terror(const char *s);
#ifdef DEBUG
void es_log(const char *text, ...);
#else
#define es_log(...)
#endif

/* soccon.c */
int sockconnect(const char *fname);

/* sockopen.c */
int sockopen(const char *fname);

/* tone.c */
int speaker_tone(int freq, int dur);
int dsp_tone(int freq, int dur, int vol);
