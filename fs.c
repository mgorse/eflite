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
 * $Id: fs.c,v 1.3 2002/03/26 00:11:01 mgorse Exp $
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>	/* tmp. for dbg. */
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#ifndef STANDALONE
#include <dlfcn.h>
#endif
#include "es.h"

#include "flite.h"
#include "flite_version.h"

#include "synthesizer.h"
#include <math.h>

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

/*static int sync_mark_no = 0;*/	/* currently used number */
/*static struct timeval mark;*/	/* time the mark has been set */

/* server-specific variables */
static cst_voice *v = NULL;
static pthread_t wave_thread;
static int wave_thread_active;
static pthread_mutex_t wave_mutex;
static cst_wave **waves;
static int wave_size;
static int wave_head, wave_tail;
static int wave_synthpos;	/* largest index to play + 1 */
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
void segfault(int sig)
{
  es_log("Got a seg fault -- exiting");
  printf("Got a seg fault -- exiting.\n");
  _exit(11);
}

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

      signal(SIGSEGV, segfault);
      flite_init();
      v = REGISTER_VOX(NULL);
      pthread_attr_init(&ta);
      pthread_attr_setdetachstate(&ta, PTHREAD_CREATE_DETACHED);
      text_thread_active = 0;
      text_size = 4096;
      text = (char *)malloc(text_size);
      text_head = text_tail = text_synthpos = 0;
      wave_thread_active = 0;
      wave_size = 64;
      waves = (cst_wave **)malloc(wave_size * sizeof(cst_wave *));
      wave_head = wave_tail = wave_synthpos = 0;
      if (!waves || !text) return NULL;
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
	if (waves) free(waves);
	text = NULL;
	waves = NULL;
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
//es_log("cst_wave_free: %8lx", (long)w);
  if (w)
  {
    cst_free(w->samples);
    cst_free(w);
  }
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
  int speed;

  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
//es_log("play: init");
  for (;!audiodev;)
  {
    pthread_mutex_lock(&wave_mutex);
    if (!waves[0])
    {
      pthread_mutex_unlock(&wave_mutex);
      pas = (wave_synthpos > 0? 1: 0);
      return NULL;
    }
    /* N.b. Following assumes that all samples have the same format */
    audiodev = audio_open(waves[0]->sample_rate, waves[0]->num_channels, CST_AUDIO_LINEAR16);
    pthread_mutex_unlock(&wave_mutex);
    if (!audiodev || (int)audiodev->platform_data == -1)
    {
      if (errno == EBUSY)
      {
//es_log("Audio device is busy; trying again");
	if (audiodev) audio_close(audiodev);
	audiodev = NULL;
	usleep(20000);
	continue;
      }
    terror("audio_open");
    }
  }
//es_log("play: %d %d", wave_head, wave_tail);
  while (wave_head < wave_synthpos)
  {
//es_log("play: wave_head=%d", wave_head);
    if (waves[wave_head] == NULL)
    {
      /* There will be more data, but it isn't ready yet. */
//es_log("wave[%d] is NULL, setting pas", wave_head);
      pas = 1;
//es_log("play: locking wave mutex");
      pthread_mutex_lock(&wave_mutex);
//es_log("play: got wave mutex");
      close_audiodev();
      pthread_mutex_unlock(&wave_mutex);
//es_log("play: unlocked wave mutex");
      return NULL;
    }
    speed = waves[wave_head]->sample_rate;
    playlen = waves[wave_head]->num_samples - (3000000 / ((synth_t *)s)->state->param[S_SPEED]);
//es_log("play: wave=%8lx, samples=%8lx, num_samples=%d playlen=%d", (long)waves[wave_head], (long)waves[wave_head]->samples, waves[wave_head]->num_samples, playlen);
    if (playlen > 0 && playlen < 500) playlen += 1000000 / ((synth_t *)s)->state->param[S_SPEED];
    time_left += (float)playlen / waves[wave_head]->sample_rate;
    if (playlen > 0)
    {
audio_write(audiodev, waves[wave_head]->samples + (1500000 / ((synth_t *)s)->state->param[S_SPEED]), playlen * 2);
    }
//es_log("play: syncing");
    audio_flush(audiodev);
    time_left -= (float)playlen / waves[wave_head]->sample_rate;
    pthread_mutex_lock(&wave_mutex);
    cst_wave_free(waves[wave_head]);
    waves[wave_head++] = NULL;
    if (wave_head > (wave_size >> 1))
    {
//es_log("play: compacting wave pointers");
      memcpy(waves, waves + wave_head, (wave_tail - wave_head) * sizeof(cst_wave *));
      wave_tail -= wave_head;
      wave_synthpos -= wave_head;
      wave_head = 0;
    }
    pthread_mutex_unlock(&wave_mutex);
//es_log("play: unlocked wave mutex");
  }
//es_log("play: loop over: %d %d", wave_head, wave_tail);
  pthread_mutex_lock(&wave_mutex);
  if (wave_head == wave_tail)
  {
    wave_head = wave_tail = wave_thread_active = 0;
    close_audiodev();
  }
  else pas = 1;	/* there is more data -- need to re-enter */
  pthread_mutex_unlock(&wave_mutex);
//es_log("play: unlocked wave mutex and exiting");
  return NULL;
}

static void * synthesize(void *s)
{
  cst_wave *wptr = NULL;
  int wml = 0;	/* wave mutex locked */
  int s_count_enter = s_count;

  if (!text_tail) return NULL;
//es_log("synthesize: entering");
  wave_synthpos = 0xffff;
  while (text[text_head])
  {
//es_log("synthesize: %d %d", text_head, text_tail);
    wptr = flite_text_to_wave(text + text_head, v);
    if (time_left > 3)
    {
//es_log("time_left=%f -- going to sleep\n", time_left);
      usleep(time_left * 750000);
    }
    if (s_count != s_count_enter)
    {
//es_log("synthesize: canceling");
      cst_wave_free(wptr);
      return NULL;
    }
//es_log("synthesize: %8lx %8lx\n", (long)wptr, (long)wptr->samples);
es_log("text=%s", text + text_head);
    while (text[text_head]) text_head++;
    text_head++;	/* This is not a bug. */
    pthread_mutex_lock(&text_mutex);
    if (wave_thread_active)
    {
    wml = 1;
//es_log("synthesize: locking wave mutex");
      pthread_mutex_lock(&wave_mutex);
//es_log("synthesize: got wave mutex");
    }
    /* Compact buffer if it seems like a good thing to do */
    if (text_head > (text_size >> 1))
    {
//es_log("synthesizer: compacting buffer");
      memcpy(text, text + text_head, text_tail - text_head + 1);
      text_tail -= text_head;
      if (text_synthpos != -1) text_synthpos -= text_head;
      text_head = 0;
    }
    if (wave_size == wave_tail + 1)
    {
//es_log("synthesize: allocating more wave memory");
      wave_size <<= 1;
      waves = (cst_wave **)realloc(waves, wave_size * sizeof(cst_wave *));
      if (!waves)
      {
	fprintf(stderr, "Out of memory, wave_size=%d\n", wave_size);
	exit(1);
      }
    }
    waves[wave_tail++] = wptr;
    if (text_head == text_tail) wave_synthpos = wave_tail;
//es_log("synthesize: after adding: %d %d", wave_head, wave_tail);
    if (wml)
    {
      pthread_mutex_unlock(&wave_mutex);
//es_log("synthesize: unlocked wave mutex");
      wml = 0;
    }
    if (pas)
    {
//es_log("pas set, creating play thread");
//es_log("synthesize: locking wave mutex");
      pthread_mutex_lock(&wave_mutex);
//es_log("synthesize: got wave mutex");
      wave_thread_active = 1;
      pthread_create(&wave_thread, &ta, play, (void *)s);
      pthread_mutex_unlock(&wave_mutex);
//es_log("synthesize: unlocked wave mutex");
      pas = 0;
    }
    pthread_mutex_unlock(&text_mutex);
  }
  pthread_mutex_lock(&text_mutex);
  text_head = text_tail = text_thread_active = 0;
  pthread_mutex_unlock(&text_mutex);
//es_log("synthesize: dying: %d %d", wave_head, wave_tail);
  return NULL;
}

/*
 * ----------------------------------------------------------------------
 * Copy Text to server. Text is not spoken until a flush command
 * arrives.
 * ----------------------------------------------------------------------
 */
static int s_synth(struct synth_struct *s, unsigned char *buffer)
{
  int len;

  assert(s->state->initialized);
  len = strlen(buffer);
  if (text_thread_active) pthread_mutex_lock(&text_mutex);
  if (text_tail + len + 2 >= text_size)
  {
    text_size <<= 1;
    text = (char *)realloc(text, text_size);
    if (!text)
    {
      fprintf(stderr, "Out of memory: text_size=%d\n", text_size);
      exit(1);
    }
  }
  strcpy(text + text_tail, buffer);
  text_tail += len + 1;
  /* The below line is important.  An extra \0 indicates to the synthesize
     thread that there are no more strings. */
  text[text_tail] = '\0';
  if (!text_thread_active)
  {
    pthread_mutex_init(&text_mutex, NULL);
//es_log("s_synth: creating new text thread");
    text_thread_active = 1;
    pthread_create(&text_thread, &ta, synthesize, s);
  }
  else pthread_mutex_unlock(&text_mutex);
  return 0;
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

  if (text_tail == 0 && wave_tail == 0) return 0;
  text_synthpos = text_tail;
  if (!wave_thread_active)
  {
//es_log("s_flush: locking wave mutex");
    pthread_mutex_lock(&wave_mutex);
//es_log("s_flush: got wave mutex");
    wave_thread_active = 1;
//es_log("es_flush: creating play thread");
    result = pthread_create(&wave_thread, &ta, play, s);
  pthread_mutex_unlock(&wave_mutex);
//es_log("s_flush: unlocked wave mutex");
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

//es_log("s_clear: text=%d %d, wave=%d %d", text_head, text_tail, wave_head, wave_tail);
//es_log("s_clear: locking wave mutex");
  pthread_mutex_lock(&wave_mutex);
//es_log("s_clear: got wave mutex");
  if (text_thread_active)
  {
//es_log("canceling text thread: %d %d", text_head, text_tail);
    s_count++;
  }
  if (wave_thread_active)
  {
//es_log("canceling wave thread: %d %d", wave_head, wave_tail);
    pthread_cancel(wave_thread);
  }
  pthread_mutex_unlock(&wave_mutex);
  /* Next two lines ensure that the mutex is in an initialized state.  It
     seems that canceling a thread that is waiting on a mutex can cause
     deadlock. */
  pthread_mutex_destroy(&wave_mutex);
  pthread_mutex_init(&wave_mutex, NULL);
//es_log("s_clear: unlocked wave mutex");
  if (audiodev)
  {
    audio_drain(audiodev);
    close_audiodev();
    usleep(5000);
  }
  /* Free any wave data */
//es_log("s_clear: freeing wave data: %d", wave_tail);
  for (i = 0; i < wave_tail; i++)
  {
    if (waves[i])
    {
      if (waves[i]->samples)
      {
//es_log("s_clear: freeing samples: %8lx", (long)waves[i]->samples);
	cst_free(waves[i]->samples);
      }
      cst_free(waves[i]);
      waves[i] = NULL;
    }
  }
//es_log("mem=%d", cst_alloc_out);
  text_head = text_tail = text_synthpos = 0;
  wave_head = wave_tail = wave_synthpos = 0;
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
	feat_set_float(v->features, "duration_stretch", (float)1000 / value);
        break;
    case S_PITCH:               /* default: 100 */
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
