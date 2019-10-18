#include "language.h"

#define NPARAMS 3

typedef struct client CLIENT;
struct client
{
  int fd;
  int param[NPARAMS];
  char punct[256];
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
#define LOG_STDERR 0x10000
#ifdef DEBUG
void es_log(int flags, const char *text, ...);
#else
#define es_log(...)
#endif

#ifdef EFLITE
#include "flite/flite.h"
/* fs.c */
void add_tone_command(struct synth_struct *s, int freq, int dur, int vol);
#endif

/* soccon.c */
int sockconnect(const char *fname);

/* sockopen.c */
int sockopen(const char *fname);

/* tone.c */
void do_tone(struct synth_struct *s, int freq, int dur, int vol, int flags);
#ifdef EFLITE
cst_wave *generate_tone(int freq, int dur, int vol);
#endif
