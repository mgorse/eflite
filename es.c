/* es.c - Generic code for creating an Emacspeak server
 * $Id: es.c,v 1.10 2002/05/03 01:17:58 mgorse Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include "es.h"
#include <dlfcn.h>

/* This table basically stolen from Vocal-Eyes, and I am not even sure if it
 * is correct under Linux. */
/* tbd - use Speak-up's table instead */
char *ascii[256] = {
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "",
  "space",
  "bang",
  "quote",
  "number",
  "dollar",
  "percent",
  "and",
  "apostrophe",
  "left paren",
  "right paren",
  "star",
  "plus",
  "comma",
  "dash",
  "period",
  "slash",
  "zero",
  "one",
  "two",
  "three",
  "four",
  "five",
  "six",
  "seven",
  "eight",
  "nine",
  "colon",
  "semicolon",
  "less than",
  "equals",
  "greater than",
  "question",
  "at",
  "ay",
  "b",
  "c",
  "d",
  "e",
  "f",
  "g",
  "h",
  "i",
  "j",
  "k",
  "l",
  "m",
  "n",
  "o",
  "p",
  "q",
  "r",
  "s",
  "t",
  "u",
  "v",
  "w",
  "x",
  "y",
  "z",
  "left bracket",
  "backslash",
  "right bracket",
  "caret",
  "underline",
  "accent",
  "ay",
  "b",
  "c",
  "d",
  "e",
  "f",
  "g",
  "h",
  "i",
  "j",
  "k",
  "l",
  "m",
  "n",
  "o",
  "p",
  "q",
  "r",
  "s",
  "t",
  "u",
  "v",
  "w",
  "x",
  "y",
  "z",
  "left brace",
  "bar",
  "right brace",
  "tilda",
  "cap delta",
  "cap cedila",
  "u de arisis",
  "e accent a u",
  "a circumflex",
  "a de arisis",
  "accented a",
  "a with small circle accent",
  "cedila",
  "e circumflex",
  "e de arisis",
  "accented e",
  "i de arisis",
  "circumflex",
  "accented i",
  "cap a de arisis",
  "cap a with small circle accent",
  "cap e accent a u",
  "small a e",
  "cap a e",
  "o circumflex",
  "o de arisis",
  "accented o",
  "u circumflex",
  "accented u",
  "y de arisis",
  "cap o de arisis",
  "cap u de arisis",
  "cents sign",
  "pounds",
  "yen",
  "p sub t",
  "fancy f",
  "a accent a u",
  "i accent a u",
  "o accent a u",
  "u accent a u",
  "tilded n",
  "tilded cap n",
  "bar under a",
  "bar under o",
  "up side down question mark",
  "short horizontal with short left down",
  "short horizontal with short right down",
  "e half",
  "one quarter",
  "up side down exclamation point",
  "much less than",
  "much greater than",
  "dark shading",
  "medium shading",
  "light shading",
  "vertical bar",
  "vertical with centered left joint",
  "vertical with centered double left joint",
  "double vertical with centered left joint",
  "upper right corner with double vertical",
  "upper right corner with double horizontal",
  "double vertical with centered double left joint",
  "double vertical",
  "double upper right corner",
  "double lower right corner",
  "lower right corner with double vertical",
  "lower right corner with double horizontal",
  "upper right corner",
  "lower left corner",
  "horizontal with centered up joint",
  "horizontal with centered down joint",
  "vertical with centered right joint",
  "horizontal",
  "cross bars",
  "vertical bar with centered double right joint",
  "double vertical with centered right joint",
  "double lower left corner",
  "double upper left corner",
  "double horizontal with centered double up joint",
  "double horizontal with centered double down joint",
  "double vertical with centered double right joint",
  "double horizontal",
  "double cross form",
  "double horizontal with centered up joint",
  "horizontal with centered up joint",
  "double horizontal with centered down joint",
  "horizontal with centered double down joint",
  "lower left corner with double vertical",
  "lower left corner with double horizontal",
  "per left corner with double horizontal",
  "upper left corner with double vertical",
  "cross with double vertical",
  "cross with double horizontal",
  "lower right corner",
  "upper left corner",
  "filled square",
  "filled lower half",
  "filled left half",
  "filled right half",
  "filled upper half",
  "alpha",
  "beta",
  "cap gamma",
  "pi",
  "cap sigma",
  "sigma",
  "mu",
  "tau",
  "cap phi",
  "theta",
  "cap omega",
  "delta",
  "infinity",
  "phi",
  "epsilon",
  "intersection",
  "is identical to",
  "plus minus",
  "greater than or equal",
  "less than or equal",
  "upper part of integral",
  "lower part of integral",
  "divide",
  "approximately equal",
  "degrees",
  "small dot",
  "tiny dot",
  "square root",
  "exponent n",
  "exponent 2",
  "filled square",
  "ascii 255"
};

static lang_t *lang;
static int default_param[NPARAMS];
  static int numclients = 0;	/* Number of clients active */
static int text_buffered = 0;
static char *buf = NULL;
static int bufsize = 200;
  CLIENT *client = NULL;
  char *sockname = NULL;
int sock = -1;
setting *settings = NULL;
char *punct_some = NULL;	/* List of punctuation to speak when "some" selected */
char *punct_all = NULL;	/* List of punctuation to speak when "all" selected */
static int tone_volume;
static int tone_flags = 3;	/* 0x01 == speaker, 0x02 = sound card */

void finish(int sig)
{
  int i;

  for (i = 0; i < numclients; i++) close(client[i].fd);
  close(sock);
  unlink(sockname);
  lang->synth->close(lang->synth);
  exit(0);
}

void setting_add(char *name, char *value)
{
  setting *p = settings;

  if (p == NULL) p = settings = (setting *)malloc(sizeof(setting));
  else
  {
    while (p->next) p = p->next;
    p->next = (setting *)malloc(sizeof(setting));
    p = p->next;
  }
  p->name = strdup(name);
  p->value = strdup(value);
  p->next = NULL;
}

int settings_init()
{
  FILE *fp;
  char buf[513];
  char *p;

  p = getenv("HOME");
  sprintf(buf, "%s/.es.conf", p);
  fp = fopen(buf, "r");
  if (!fp) fp = fopen("/etc/es.conf", "r");
  if (!fp) return 1;
  while (fgets(buf, 512, fp))
  {
    p = buf;
    while (*p >= 32) p++;
    if (*p == 13 || *p == 10) *p = '\0';
    p = buf;
    if (!strncasecmp(buf, "env ", 4))
    {
      putenv(strdup(buf + 4));
      continue;
    }
    while (*p && *p != '=') p++;
    if (*p != '=') continue;
    *p++ = '\0';
    setting_add(buf, p);
  }
  fclose(fp);
  return 0;
}

char *lookup_string(void *context, const char *name)
{
  setting *p;

  if (settings == NULL)
  {
    if (settings_init() == -1)
    {
      fprintf(stderr, "Fatal:  Cannot initialize settings\n");
      exit(1);
    }
  }
  p = settings;
  while (p)
  {
    if (!strcasecmp(p->name, name)) return p->value;
    p = p->next;
  }
  return NULL;
}

int lookup_int(char *name, int defval)
{
  char *val;

  val = lookup_string(NULL, name);
  if (!val) return defval;
  return atoi(val);
}

void es_synthesize()
{
  lang->synth->flush(lang->synth);
  text_buffered = 0;
}

void es_addtext(CLIENT *client, char *buf)
{
  int i;
  int val;
  char obuf[1024];
  char *p, *q;

  for (i = 0; i < NPARAMS; i++)
  {
    lang->synth->get_param(lang->synth, i, &val);
    if (val != client->param[i])
    {
      if (text_buffered) es_synthesize();
      lang->synth->set_param(lang->synth, i, client->param[i]);
    }
  }
  q = obuf;
  for (p = buf; *p; p++)
  {
    if (q - obuf > 896)
    {
      *q = 0;
      lang->synth->synth(lang->synth, obuf);
      q = obuf;
    }
    if (*p < 0 || !client->punct[(int)*p]) *q++ = *p;
    else
    {
      if (q > obuf && q[-1] != ' ') *q++ = ' ';
      strcpy(q, ascii[(int)*p]);
      while (*q) q++;
      *q++ = ' ';
    }
  }
  *q = 0;
  lang->synth->synth(lang->synth, obuf);
  text_buffered = 1;
}

#ifdef DEBUG
void es_log(int debuglevel, const char *text, ...)
{
#ifdef DEBUG
  char buf[200];
  va_list arg;
  FILE *fp;
  char logname[200];

  if (debuglevel > DEBUG) return;
  sprintf(logname, "%s/es.log", getenv("HOME"));
  va_start(arg, text);
  vsnprintf(buf, 200, text, arg);
  va_end(arg);
  fp = fopen(logname, "a");
  if (!fp) return;
  fputs(buf, fp);
  fprintf(fp, "\n");
  fclose(fp);
#endif
}
#endif

void client_init(CLIENT *client)
{
  memcpy(&client->param, &default_param, sizeof(*default_param) * 3);
  memset(client->punct, 0, sizeof(client->punct));
}

static int punct_add(CLIENT *client, char *str)
{
  for (;*str;str++)
  {
    if (*str < 0) return -1;
    client->punct[(int)*str] = 1;
  }
  return 0;
}

/* tbc - think about processing some of these, perhaps, or modifying the
   Emacspeak server so that it doesn't send DEC-talk commands */
static void remove_dectalk_codes(char *buf)
{
  int in_brackets = 0;

  while (*buf)
  {
    switch (*buf)
    {
    case '[': *buf = ' '; in_brackets = 1; break;
    case ']': *buf = ' '; in_brackets = 0; break;
    default: if (in_brackets) *buf = ' ';
    }
    buf++;
  }
}

void parse(CLIENT *client, char *buf)
{
  int i;
  int count;
  char *token[10];
  int state;
  int tmp;
  char *p;

  if (buf[0] == 'l' && buf[1] == ' ')
  {
    p = buf + 2;
    if (*p == '{' && p[1]) p++;
    tmp = client->param[1];
    if (*p >= 65 && *p <= 90)
    {
      client->param[1] *= 12;
      client->param[1] /= 10;
    }
    es_addtext(client, ascii[(unsigned char)*p]);
    es_synthesize();
    client->param[1] = tmp;
  }
  memset(token, 0, sizeof(token));
  for (i = state = count = 0; buf[i]; i++)
  {
    switch (state)
    {
    case 0:
      if (buf[i] == ' ')
      {
	buf[i] = 0;
	state = 1;
	break;
      }
      break;
    case 1:
      if (buf[i] == '{')
      {
	if (count < 9)
	  token[count++] = buf + i + 1;
	state = 2;
      }
      else if (buf[i] != ' ')
      {
	if (count < 9) token[count++] = buf + i;
	state = 0;
      }
      break;
    case 2:
      if (buf[i] == '}')
      {
	buf[i] = 0;
	state = 1;
      }
      break;
    }
  }
  if (!strcmp (buf, "q") && token[0])
  {
    remove_dectalk_codes(token[0]);
    es_addtext(client, token[0]);
  }
  else if (!strcmp (buf, "d"))
  {
    es_synthesize();
  }
  else if (!strcmp(buf, "tts_say") && token[0])
  {
    remove_dectalk_codes(token[0]);
    es_addtext(client, token[0]);
    es_synthesize();
  }
  else if ((!strcmp (buf, "r") || !strcmp(buf, "tts_set_speech_rate")) && token[0])
  {
    if (text_buffered) es_synthesize(client);
    /* In the libspeech api, a rate of 1000 is defined as "normal."  Thus, fs.c
	defines a rate of 1000 as equivalent to FLite's normal rate (about 175
	wpm).  The following conversion is accurate for it but may not be
	accurate for other synths.  It may be useful to add an api function to
	get the proper rate in wpm. */
    client->param[0] = (atoi(token[0]) * 23) / 4;
  }
  else if (!strcmp (buf, "reset"))
  {
    client_init(client);
  }
  else if (!strcmp (buf, "s"))
  {
es_log(1, "silent");
    lang->synth->clear(lang->synth);
  }
  else if (!strcmp(buf, "tts_set_punctuations") && token[0])
  {
    memset(client->punct, 0, sizeof(client->punct));
    if (strcasecmp(token[0], "none")) punct_add(client, punct_some);
    if (!strcasecmp(token[0], "all")) punct_add(client, punct_all);
  }
  else if (!strcasecmp(buf, "t") && token[0] && token[1])
  {
    int freq = atoi(token[0]);
    int dur = atoi(token[1]);
    /* The following attempts to make a tone using the PC speaker.  This
       requires root priveleges.  If it fails, then we attempt to write a tone
       to the sound card. */
    /* tbc - For the FLite server, we could use the FLite audio library for
       this, but that would introduce a dependency on FLite. */
    /* tbd - allow user to set volume */
    do_tone(lang->synth, freq, dur, tone_volume, tone_flags);
  }
}

static int string_is_complete(char *buf, int size)
{
  int i;

  if (buf[size-1] != 13 && buf[size-1] != 10) return 0;
  for (i = size-2;i >= 0;i--)
  {
    if (buf[i] == '}') return 1;
    if (buf[i] == '{') return 0;
  }
  return 1;
}

int handle(CLIENT *client)
{
  int i, j;
  int size;
  char *p;
  int in_braces = 0;
  int result;

  size = read(client->fd, buf, bufsize - 1);
  if (!size) return 1;
  buf[size] = '\0';
  /* If we did not get a complete line, then assume that the buffer filled up
     and that there is more to come. */
  while (!string_is_complete(buf, size))
  {
    bufsize += 200;
    buf = realloc (buf, bufsize);
    if (!buf) exit(1);
    result = read(client->fd, buf + size, 200);
    if (result <= 0)
    {
      buf[size] = '\0';
      break;
    }
    size += result;
    buf[size] = 0;
    }
es_log(1, "handle: %s", buf);
  p = buf;
  for (i = j = 0; i < size; i++)
  {
    if ((buf[i] == 13 || buf[i] == 10) && !in_braces)
    {
      if ((*p == 'l' || *p == 'q') && *(p + 1) == ' ')
      {
	for (j = size - 2; j >= i; j--)
	{
	  if (buf[j] == '{') in_braces = 1;
	  else if (buf[j] == '}') in_braces = 0;
	  else if (!in_braces && buf[j] == 13 && buf[j + 1] == 's' && buf[j + 2] == 13)
	  {
	    i = j + 2;
	  }
	}
      }
      buf[i] = in_braces = 0;
      if (i > j)
      {
	parse(client, p);
      }
      p = buf + i + 1;
    }
    else if (buf[i] == '{') in_braces = 1;
    else if (buf[i] == '}') in_braces = 0;
  }
  return 0;
}

/* Like perror() but also log the error and exit */
void terror(const char *s)
{
  int errnum = errno;

  es_log(1, "%s: %s", s, strerror(errnum));
  fprintf(stderr, "%s: %s\n", s, strerror(errnum));
  exit(errnum);
}

void passthrough(char *infile, int outfd)
{
  char buf[500];
  int size;
  int fd;

  signal(SIGCHLD, finish);
  es_log(1, "es: reading input from %s", infile);
  fd = open(infile, O_RDONLY);
  if (fd == -1) terror("open");
  while (1)
  {
    size = read(fd, buf, sizeof(buf));
    if (size == -1) terror("read");
    if (size == 0)
    {
      int is_fifo;
      struct stat stat;
      fstat(fd, &stat);
      is_fifo = S_ISFIFO(stat.st_mode);
      close(fd);
      if (is_fifo)
      {
	/* Re-open it */
	fd = open(infile, O_RDONLY);
	continue;
      }
      /* Otherwise terminate */
      exit(0);
    }
    if (buf[0] == 3) break;
    if (write(outfd, buf, size) != size) terror("write");
  }
  exit(0);
}

int is_dir(char *name)
{
  struct stat st;

  stat(name, &st);
  return S_ISDIR(st.st_mode);
}

int main (int argc, char *argv[])
{
  fd_set fds;
  int i;
  int maxclients = 0;	/* Number of clients with space allocated */
  char *infile = "/dev/stdin";
  int max;
  int local_fd;
  int child;
  char *input = NULL;
  int more_opts = 1;

  while (more_opts) switch(getopt(argc, argv, "f:"))
  {
  case 'f':
    input = optarg;
    break;
  default: more_opts = 0;
  }
  /* The following allows an input file name to be specified on the command
     line.  If "-" is specified, then stdin is read from, as if no argument
     was passed. */
  if (input && strcmp(input, "-") && !is_dir(input)) infile = input;
  buf = (char *)malloc(bufsize);
  if (buf == NULL) exit(1);
  sockname = lookup_string(NULL, "socketfile");
  if (!sockname) sockname = "/tmp/es.socket";
  local_fd = sockconnect(sockname);
  if (local_fd != -1) passthrough(infile, local_fd);
  if (!(child = fork()))
  {
    usleep(200000);
    if (infile)
    {
      local_fd = sockconnect(sockname);
      if (local_fd == -1)
      {
	fprintf(stderr, "Daemon not accepting connections -- exiting\n");
	exit(1);
      }
      passthrough(infile, local_fd);
    }
    exit(0);
  }
  punct_some = lookup_string(NULL, "punct_some");
  if (punct_some == NULL) punct_some = "@#$%^&_[]{}\\|";
  punct_all = lookup_string(NULL, "punct_all");
  if (!punct_all) punct_all = "!@#$%^&*()-=_+[]\\|{};':\",./<>?";
  tone_volume = lookup_int("tone_volume", 8192);
  if (lookup_int("speaker_tones", 1) == 0) tone_flags &= 0xfe;
  if (lookup_int("soundcard_tones", 1) == 0) tone_flags &= 0xfd;

  unlink(sockname);
  sock = sockopen(sockname);
  if (sock == -1)
  {
    fprintf(stderr, "Error opening socket: %s\n", sockname);
    exit(1);
  }
  /* The following line doesn't seem to work.  Why not? */
  fchmod(sock, 0666);
  es_log(1, "Socket initialized\n");
  signal(SIGINT, finish);
  signal(SIGTERM, finish);
  //signal (SIGHUP, SIG_IGN);
  lang = language_open(NULL, lookup_string);
  if (lang == NULL)
  {
    fprintf (stderr, "Error initializing language\n");
    exit (1);
  }

  for (i = 0; i < NPARAMS; i++) lang->synth->get_param(lang->synth, i, &default_param[i]);
  for (;;)
  {
    FD_ZERO(&fds);
    max = sock;
    FD_SET(sock, &fds);
    for (i = 0; i < numclients; i++)
    {
      if (client[i].fd > max) max = client[i].fd;
      FD_SET(client[i].fd, &fds);
    }
    select(max + 1, &fds, NULL, NULL, NULL);
    if (FD_ISSET(sock, &fds))
    {
      if (numclients == maxclients)
      {
	client = realloc(client, ++maxclients * sizeof(CLIENT));
      }
es_log(1, "Accepting connection\n");
      client[numclients++].fd = accept(sock, 0, 0);
      client_init(&client[i]);
      continue;
    }
    for (i = 0; i < numclients; i++)
    {
      if (FD_ISSET(client[i].fd, &fds))
      {
	if (handle(&client[i]))
	{
es_log(1, "Deactivating a client\n");
	  /* Deactivate client */
	  close(client[i].fd);
	  memcpy(client + i, client + i + 1, sizeof(CLIENT) * (--numclients - i));
	  if (numclients == 0) finish(0);
	}
	break;
      }
    }
  }
}
