/*
 * brass - Braille and speech server
 *
 * Copyright (C) 2001 by Roger Butenuth, All rights reserved.
 * Copyright 2002 by Michael Gorse.
 *
 * This is free software, placed under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation.  Please see the file COPYING for details.
 *
 * $Id: fs.c,v 1.12 2002/10/11 01:42:03 mgorse Exp $
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>	/* tmp. for dbg. */
#include <assert.h>
#ifndef STANDALONE
#include <dlfcn.h>
#endif
#include "flite.h"
#include "flite_version.h"

#include "synthesizer.h"
#include <math.h>
#include "es.h"


static int  s_close(synth_t *s);
static int  s_synth(synth_t *s, unsigned char *buffer);
static int  s_flush(synth_t *s);
static int  s_clear(synth_t *s);
#if 0
static int  s_index_set(struct synth_struct *s);
static int  s_index_wait(struct synth_struct *s, int id, int timeout);
#endif
static int  s_get_param(struct synth_struct *s, synth_par_t par, int *value);
static int  s_set_param(struct synth_struct *s, synth_par_t par, int value);

typedef struct synth_state {
    int  param[S_MAX];
    int  initialized;
} synth_state_t;

static synth_state_t private_state[2];

static synth_t state[] = {
    {
        &private_state[0],
        &languages[LANG_BRITISH_ENGLISH],
        "FLite/US English",
        NULL,
        s_close,
        s_synth,
        s_flush,
        s_clear,
        NULL,                   /* s_index_set, */
        NULL,                   /* s_index_wait, */
        s_get_param,
        s_set_param
    }, {
        &private_state[1],
        &languages[LANG_GERMAN],
        "FLite/German",	/* not supported */
        NULL,
        s_close,
        s_synth,
        s_flush,
        s_clear,
        NULL,                   /* s_index_set, */
        NULL,                   /* s_index_wait, */
        s_get_param,
        s_set_param
    }
};


static int     current_language = -1;
static int     ref_count = 0;
static FILE    *debug_fp = NULL;

  typedef enum { NONE, SPEECH, TONE } ac_type;

typedef struct
{
  ac_type type;
  void *data;
} AUDIO_COMMAND;

#if 0	/* tbd - figure out what these variables are supposed to be used for */
/*static int sync_mark_no = 0;*/	/* currently used number */
/*static struct timeval mark;*/	/* time the mark has been set */
#endif

/* server-specific variables */
static cst_voice *v = NULL;
static pthread_t wave_thread;
static int wave_thread_active;
static pthread_mutex_t wave_mutex;
static AUDIO_COMMAND *ac;
static int ac_size;
static int ac_head, ac_tail;
static int ac_synthpos;	/* largest index to play + 1 */
static pthread_t text_thread;
static int text_thread_active;
static pthread_mutex_t text_mutex;
static char *text;
static int text_size;
static int text_head, text_tail;
static int text_synthpos;	/* pointer to beyond last piece to be played */
cst_audiodev *audiodev = NULL;	/* Sound device */
static int pas;	/* play after synthesizing? */
static pthread_attr_t ta;	/* for creating threads */
static float time_left = 0;
static int s_count = 0;

extern cst_voice *REGISTER_VOX(const char *voxdir);
extern int cst_alloc_out;

#ifndef STANDALONE
/*
 * ----------------------------------------------------------------------
 * Called before library is loaded.
 * ----------------------------------------------------------------------
 */
void _init(void)
{
}


/*
 * ----------------------------------------------------------------------
 * Called before library is unloaded.
 * ----------------------------------------------------------------------
 */
void _fini(void)
{
}
#endif

/*
 * ----------------------------------------------------------------------
 * General open function for german and english synthesizer.
 * Second open increments refcount.
 * Return 0 on success, 1 on error.
 * ----------------------------------------------------------------------
 */
#ifdef DEBUG
/* This code seems to cause lingering processes in the event of a crash */
void segfault(int sig)
{
  extern char *sockname;
  es_log(1, "Got a seg fault -- exiting");
  printf("Got a seg fault -- exiting.\n");
  unlink(sockname);
  _exit(11);
}
#endif

synth_t *synth_open(void *context, lookup_string_t lookup)
{
    synth_t *s;
    char    *language = (*lookup)(context, "language");
    int     langi;

    debug_fp = stderr;
    if (language == NULL) {
	language = "english";
    }
    if (ref_count == 0) {
      unlink("log");

#ifdef DEBUG
      signal(SIGSEGV, segfault);
#endif
      flite_init();
      v = REGISTER_VOX(NULL);
      pthread_attr_init(&ta);
      pthread_attr_setdetachstate(&ta, PTHREAD_CREATE_DETACHED);
      text_thread_active = 0;
      text_size = 4096;
      text = (char *)malloc(text_size);
      text_head = text_tail = text_synthpos = 0;
      wave_thread_active = 0;
      ac_size = 64;
      ac = (AUDIO_COMMAND *)malloc(ac_size * sizeof(AUDIO_COMMAND));
      ac_head = ac_tail = ac_synthpos = 0;
      if (!ac || !text) return NULL;
      pthread_mutex_init(&wave_mutex, NULL);
      time_left = 0;
    }
    ref_count++;

    if (!strcasecmp(language, "english")) {
        langi = 0;
        s = &state[langi];
    } else if (!strcasecmp(language, "german")) {
        langi = 1;
        s = &state[langi];
    } else {
        langi = -1;
        s = NULL;
    }

    if (s != NULL && !s->state->initialized) {
        s->state->param[S_SPEED]  = 1000;
        s->state->param[S_PITCH]  = 1000;
        s->state->param[S_VOLUME] = 1000;
        s->state->initialized = 1;
    }

    return s;
}


/*
 * ----------------------------------------------------------------------
 * General close. Decrement refcount, do real close when count reaches
 * zero.
 * ----------------------------------------------------------------------
 */
static int s_close(synth_t *s)
{
    ref_count--;
    if (ref_count == 0) {
	/* Wait for any speech to be spoken */
	while (text_thread_active || wave_thread_active) usleep(50000);
	if (text) free(text);
	if (ac) free(ac);
	text = NULL;
	ac = NULL;
    }

    return 0;
}


/*
 * ----------------------------------------------------------------------
 * Verify that the synthesizer is set to the correct language.
 * Switch if necessary.
 * ----------------------------------------------------------------------
 */
static void verify_language(struct synth_struct *s)
{
    int value = -1;

    if (s->lang->lang == LANG_BRITISH_ENGLISH
        && current_language != LANG_BRITISH_ENGLISH) {
	/* tbd */
    } else if (s->lang->lang == LANG_GERMAN &&
               current_language != LANG_GERMAN) {
	/* tbd */
    }
    if (value != -1) {
	/* tbd */
    }
}

static void cst_wave_free(cst_wave *w)
{
  es_log(2, "cst_wave_free: %p", w);
  if (w)
  {
    cst_free(w->samples);
    cst_free(w);
  }
}

static void ac_destroy(AUDIO_COMMAND *ac)
{
  switch (ac->type)
  {
  case SPEECH: case TONE: cst_wave_free(ac->data); break;
  default: break;
  }
  ac->type = NONE;
}

static void close_audiodev()
{
  if (audiodev)
  {
    audio_close(audiodev);
    audiodev = NULL;
  }
}

static void * play(void *s)
{
  int playlen;
  int skip;
  cst_wave *wptr;
  int *sparam = ((synth_t *)s)->state->param;

  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
  es_log(2, "play: init: ac-head=%d ac_tail=%d", ac_head, ac_tail);
  while (ac_head < ac_synthpos)
  {
    for (;!audiodev;)
    {
      pthread_mutex_lock(&wave_mutex);
      if (ac[ac_head].type == NONE)
      {
	pthread_mutex_unlock(&wave_mutex);
	pas = (ac_synthpos > 0? 1: 0);
	return NULL;
      }
      wptr = ac[ac_head].data;
      audiodev = audio_open(wptr->sample_rate, wptr->num_channels, CST_AUDIO_LINEAR16);
      pthread_mutex_unlock(&wave_mutex);
      if (!audiodev || (int)audiodev->platform_data == -1)
      {
	if (errno == EBUSY)
	{
	  es_log(2, "Audio device is busy; trying again");
	  close_audiodev();
	  usleep(20000);
	  continue;
	}
      terror("audio_open");
      }
    }
    es_log(2, "play: ac_head=%d type=%d", ac_head, ac[ac_head].type);
    if (ac[ac_head].type == NONE)
    {
      /* There will be more data, but it isn't ready yet. */
      es_log(2, "ac[%d] is inactive, setting pas", ac_head);
      pas = 1;
      es_log(2, "play: locking wave mutex");
      pthread_mutex_lock(&wave_mutex);
      es_log(2, "play: got wave mutex");
      close_audiodev();
      pthread_mutex_unlock(&wave_mutex);
      es_log(2, "play: unlocked wave mutex");
      return NULL;
    }
    wptr = ac[ac_head].data;
    if (ac[ac_head].type == SPEECH)
    {
      skip = 1500000 / sparam[S_SPEED];
      playlen = wptr->num_samples - (skip * 2);
      if (playlen > 0 && playlen < 500) playlen += (skip * 2) / 3;
    }
    else
    {
      skip = 0;
      playlen = wptr->num_samples;
    }
    if (playlen < 0) playlen = 0;
    es_log(2, "play: wave=%p, samples=%p, num_samples=%d skip=%d playlen=%d", wptr, wptr->samples, wptr->num_samples, skip, playlen);
    time_left += (float)playlen / wptr->sample_rate;
    if (playlen > 0)
    {
      if (sparam[S_VOLUME] != 1000)
	cst_wave_rescale(wptr, (sparam[S_VOLUME] << 16) / 1000);
      audio_write(audiodev, wptr->samples + skip, playlen * 2);
    }
    es_log(2, "play: syncing");
    audio_flush(audiodev);
    time_left -= (float)playlen / wptr->sample_rate;
    pthread_mutex_lock(&wave_mutex);
    ac_destroy(&ac[ac_head]);
    close_audiodev();
    if (++ac_head > (ac_size >> 1))
    {
      es_log(1, "play: compacting wave pointers");
      memmove(ac, ac + ac_head, (ac_tail - ac_head) * sizeof(AUDIO_COMMAND));
      ac_tail -= ac_head;
      ac_synthpos -= ac_head;
      ac_head = 0;
    }
    pthread_mutex_unlock(&wave_mutex);
    es_log(2, "play: unlocked wave mutex");
  }
  es_log(2, "play: loop over: %d %d", ac_head, ac_tail);
  pthread_mutex_lock(&wave_mutex);
  if (ac_head == ac_tail)
  {
    ac_head = ac_tail = wave_thread_active = 0;
  }
  else pas = 1;	/* there is more data -- need to re-enter */
  pthread_mutex_unlock(&wave_mutex);
  es_log(2, "play: unlocked wave mutex and exiting");
  return NULL;
}

/* This kludge works around the race condition where a synthesize thread is
   started, then speech is silenced, then more speech is added, with the first
   thread never having been created.  We want to avoid having two synthesize
   threads running in this situation.  A better solution would be to use
   thread conditions, but I have been unable to get them to work so far.  If
   you can help, then send me an email. */
typedef struct
{
  struct synth_t *s;
  int s_count;
} SYNTHESIZE_INITIALIZER;

void *make_initializer(void *s, int s_count)
{
  SYNTHESIZE_INITIALIZER *ptr;

  ptr = (SYNTHESIZE_INITIALIZER *)malloc(sizeof(SYNTHESIZE_INITIALIZER));
  if (!ptr) return NULL;
  ptr->s = s;
  ptr->s_count = s_count;
  return ptr;
}

static void * synthesize(void *initializer)
{
  cst_wave *wptr = NULL;
  int wml = 0;	/* wave mutex locked */
  struct synth_t *s;
  int s_count_enter;
  int command;
  int just_entered = 1;

  if (!initializer) return NULL;
  s = ((SYNTHESIZE_INITIALIZER *)initializer)->s;
  s_count_enter = ((SYNTHESIZE_INITIALIZER *)initializer)->s_count;
  free(initializer);
  if (s_count != s_count_enter) return NULL;
  es_log(2, "synthesize: entering");
  while (text[text_head])
  {
    es_log(2, "synthesize: %d %d", text_head, text_tail);
    switch ((command = text[text_head++]))
    {
    case 1:	/* text */
      wptr = flite_text_to_wave(text + text_head, v);
      break;
    case 2:	/* tone */
    {
      int freq, dur, vol;
      if (sscanf(text + text_head, "%d %d %d", &freq, &dur, &vol) != 3)
      {
	es_log(1, "unable to scan tone: %s", text + text_head);
	break;
      }
      wptr = generate_tone(freq, dur, vol);
      break;
      }
    default:
      /* snafu - I'm getting the hell out of here */
      es_log(1, "synthesize: internal error: unknown command: %x", text[text_head - 1]);
      return NULL;
    }
    if (time_left > 3 && !just_entered)
    {
      es_log(1, "time_left=%f -- going to sleep\n", time_left);
      usleep(time_left * 750000);
    }
    just_entered = 0;
    if (s_count != s_count_enter)
    {
      es_log(2, "synthesize: canceling");
      cst_wave_free(wptr);
      return NULL;
    }
    es_log(2, "synthesize: %p %p\n", wptr, wptr->samples);
    es_log(1, "text=%s", text + text_head);
    while (text[text_head]) text_head++;
    text_head++;	/* This is not a bug. */
    pthread_mutex_lock(&text_mutex);
    if (wave_thread_active)
    {
    wml = 1;
    es_log(2, "synthesize: locking wave mutex");
      pthread_mutex_lock(&wave_mutex);
    es_log(2, "synthesize: got wave mutex");
    }
    /* Compact buffer if it seems like a good thing to do */
    if (text_head > (text_size >> 1))
    {
      es_log(1, "synthesizer: compacting buffer");
      memcpy(text, text + text_head, text_tail - text_head + 1);
      text_tail -= text_head;
      if (text_synthpos != -1) text_synthpos -= text_head;
      text_head = 0;
    }
    if (ac_size == ac_tail + 1)
    {
      es_log(1, "synthesize: allocating more wave memory");
      ac_size <<= 1;
      ac = (AUDIO_COMMAND *)realloc(ac, ac_size * sizeof(AUDIO_COMMAND));
      if (!ac)
      {
	fprintf(stderr, "Out of memory, ac_size=%d\n", ac_size);
	exit(1);
      }
    }
    ac[ac_tail].type = command;
    ac[ac_tail++].data = wptr;
    if (text_head == text_tail) ac_synthpos = ac_tail;
    es_log(2, "synthesize: after adding: %d %d", ac_head, ac_tail);
    if (wml)
    {
      pthread_mutex_unlock(&wave_mutex);
      es_log(2, "synthesize: unlocked wave mutex");
      wml = 0;
    }
    if (pas)
    {
      es_log(2, "synthesize: pas set, creating play thread");
      es_log(2, "synthesize: locking wave mutex");
      pthread_mutex_lock(&wave_mutex);
      es_log(2, "synthesize: got wave mutex");
      wave_thread_active = 1;
      pthread_create(&wave_thread, &ta, play, (void *)s);
      pthread_mutex_unlock(&wave_mutex);
      es_log(2, "synthesize: unlocked wave mutex");
      pas = 0;
    }
    pthread_mutex_unlock(&text_mutex);
  }
  pthread_mutex_lock(&text_mutex);
  text_head = text_tail = text_thread_active = 0;
  pthread_mutex_unlock(&text_mutex);
  es_log(2, "synthesize: dying: %d %d", ac_head, ac_tail);
  return NULL;
}

/*
 * ----------------------------------------------------------------------
 * Copy Text to server. Text is not spoken until a flush command
 * arrives.
 * ----------------------------------------------------------------------
 */
static void add_command(struct synth_struct *s, int id, unsigned char *buffer)
{
  int len;

  assert(s->state->initialized);
  len = strlen(buffer);
  if (text_thread_active) pthread_mutex_lock(&text_mutex);
  if (text_tail + len + 3 >= text_size)
  {
    text_size <<= 1;
    text = (char *)realloc(text, text_size);
    if (!text)
    {
      fprintf(stderr, "Out of memory: text_size=%d\n", text_size);
      exit(1);
    }
  }
  text[text_tail++] = (char)id;
  strcpy(text + text_tail, buffer);
  text_tail += len + 1;
  /* The below line is important.  An extra \0 indicates to the synthesize
     thread that there is no more data. */
  text[text_tail] = '\0';
  if (!text_thread_active)
  {
    pthread_mutex_init(&text_mutex, NULL);
    es_log(2, "s_synth: creating new text thread");
    text_thread_active = 1;
    ac_synthpos = 0xffff;
    pthread_create(&text_thread, &ta, synthesize, make_initializer(s, s_count));
  }
  else pthread_mutex_unlock(&text_mutex);
  return;
}

static int s_synth(struct synth_struct *s, unsigned char *buffer)
{
  add_command(s, 1, buffer);
  return 0;
}

void add_tone_command(struct synth_struct *s, int freq, int dur, int vol)
{
  char buf[40];

  sprintf(buf, "%d %d %d", freq, dur, vol);
  add_command(s, 2, buf);
}

/*
 * ----------------------------------------------------------------------
 * Flush synthesizer. This triggers the synthesizer and starts 
 * the synthesis.
 * ----------------------------------------------------------------------
 */
static int s_flush(synth_t *s)
{
  int result;

  if (text_tail == 0 && ac_tail == 0) return 0;
  text_synthpos = text_tail;
  if (!wave_thread_active)
  {
    es_log(2, "s_flush: locking wave mutex");
    pthread_mutex_lock(&wave_mutex);
    es_log(2, "s_flush: got wave mutex");
    wave_thread_active = 1;
    es_log(2, "es_flush: creating play thread");
    result = pthread_create(&wave_thread, &ta, play, s);
  pthread_mutex_unlock(&wave_mutex);
    es_log(2, "s_flush: unlocked wave mutex");
  return result;
  }
    return 0;
}


/*
 * ----------------------------------------------------------------------
 * Remove anything in the synthesizer speech queue.
 * ----------------------------------------------------------------------
 */
static int s_clear(synth_t *s)
{
  int i;

  es_log(2, "s_clear: text=%d %d, wave=%d %d", text_head, text_tail, ac_head, ac_tail);
  es_log(2, "s_clear: locking wave mutex");
  pthread_mutex_lock(&wave_mutex);
  es_log(2, "s_clear: got wave mutex");
  if (text_thread_active)
  {
    es_log(2, "canceling text thread: %d %d", text_head, text_tail);
    s_count++;
  }
  if (wave_thread_active)
  {
    es_log(2, "canceling wave thread: %d %d", ac_head, ac_tail);
    pthread_cancel(wave_thread);
  }
  pthread_mutex_unlock(&wave_mutex);
  /* Next two lines ensure that the mutex is in an initialized state.  It
     seems that canceling a thread that is waiting on a mutex can cause
     deadlock. */
  pthread_mutex_destroy(&wave_mutex);
  pthread_mutex_init(&wave_mutex, NULL);
  es_log(2, "s_clear: unlocked wave mutex");
  if (audiodev)
  {
    audio_drain(audiodev);
    close_audiodev();
    usleep(5000);
  }
  /* Free any wave data */
//es_log("s_clear: freeing wave data: %d", ac_tail);
  for (i = 0; i < ac_tail; i++)
  {
    if (ac[i].type != NONE) ac_destroy(&ac[i]);
  }
#if 0
  //es_log(2, "mem=%d", cst_alloc_out);
#endif
  text_head = text_tail = text_synthpos = 0;
  ac_head = ac_tail = ac_synthpos = 0;
  text_thread_active = wave_thread_active = 0;
  pas = 0;
  time_left = 0;
    return 0;
}


#if 0
/*
 * ----------------------------------------------------------------------
 * ToDo
 * ----------------------------------------------------------------------
 */
static int s_index_set(struct synth_struct *s)
{
    return 0;
}

/*
 * ----------------------------------------------------------------------
 * ToDo
 * ----------------------------------------------------------------------
 */
static int s_index_wait(struct synth_struct *s, int id, int timeout)
{
    int res = 0;
    return res;
}
#endif


/*
 * ----------------------------------------------------------------------
 * Get a synthesizer parameter.
 * ----------------------------------------------------------------------
 */
static int s_get_param(struct synth_struct *s, synth_par_t par, int *value)
{
    if (par >= 0 && par < S_MAX) {
	*value = s->state->param[par];
	return 0;
    } else
	return 1;
}


/*
 * ----------------------------------------------------------------------
 * Set a parameter of the synthesizer.
 * ----------------------------------------------------------------------
 */
static int s_set_param(struct synth_struct *s, synth_par_t par, int value)
{
    verify_language(s);

    switch (par) {
    case S_SPEED:               /* default: 1 */
	es_log(2, "Setting duration_stretch to %4.3f", (float)1000 / value);
	feat_set_float(v->features, "duration_stretch", (float)1000 / value);
        break;
    case S_PITCH:               /* default: 100 */
      es_log(2, "Setting pitch to %3.3f", exp((float)value / 1000) * 100 / exp(1));
	feat_set_float(v->features, "int_f0_target_mean", exp((float)value / 1000) * 100 / exp(1));
	/* tbd */
        break;
    case S_VOLUME:              /* default: 92, range: 0-100 */
	/* tbd */
        break;
    default:
        return 1;
    }
    s->state->param[par] = value;

    return 0;
}

#ifdef STANDALONE
lang_descr_t languages[LANGUAGES] = {
    { LANG_BRITISH_ENGLISH, "British English" },
    { LANG_GERMAN, "German" },
    { LANG_DUMMY, "no language" }
};

lang_t lang =
{
  NULL,
  &languages[0],
NULL,
  NULL,
  0,
  0,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

lang_t *language_open(void *context, lookup_string_t lookup)
{
  lang.synth = synth_open(context, lookup);
  return &lang;
}
#endif
