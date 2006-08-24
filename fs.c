/*
 * brass - Braille and speech server
 *
 * Copyright (C) 2001 by Roger Butenuth, All rights reserved.
 * Copyright 2002 by Michael Gorse.
 * Copyright 2006 by Lukas Loehrer
 *
 * This is free software, placed under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation.  Please see the file COPYING for details.
 *
 * $Id: fs.c 27 2006-05-10 14:13:25Z luke $
 *
 * Notes:
 *
 * The server process reads commands from clients and puts them into a
 * temporary buffer, defined by the variables text, text_head,
 * text_tail and text_synthpos. The server spawns two threads that are
 * responsible for further processing.
 *
 * The text thread (text_thread) reads commands from the text
 * buffer. Accesses to this buffer are synchronized by text_mutex. The
 * text thread calls the flite library to convert the text to
 * speech. The generated wave forms are stored in the wave buffer (ac,
 * ac_head, ac_tail, ac_synthpos, time_left). Access to this buffer is
 * synchronized by wave_mutex.
 *
 * The wave thread (wave_thread) takes the wave forms from the wave
 * buffer and plays them via the flite audio interface.
 *
 * The text thread is created when text data first arrives in the text
 * buffer (in function add_command()). It keeps running until a
 * "silence command is received. When the text buffer gets empty, the
 * text thread goes to sleep on the condition variable
 * "_text_condition". It is woke up by the main process when new data
 * arrives.
 *
 * The wave thread works similarly to the text thread. It is created
 * in s_flush and sleeps on "wave_condition" when the wave buffer
 * becomes empty.
 *
 * The "silence" command (handled in s_clear()) terminates both
 * threads. The wave thread is cancelled with pthread_cancel() and the
 * text thread is told to exit by setting text_thread_cancel to a
 * true value. Both threads are joined to be sure they have
 * exited. The state of all buffers is reset.
 *
 * The "time_left" variable holds the number of seconds of speech data
 * in the wave buffer. If this value exeeds a certain threshold
 * (currently 30s), the text thread will stop synthesizing more speech
 * and will go to sleep on text_condition. The wave thread signals
 * to the text thread when the wave buffer shrinks. This mechanism
 * prevents us from synthesizing useless wave forms we may never have
 * to play and reduces the memory usage of eflite drastically in
 * situations where the user requests a long document to be read from
 * top to bottom.
 *
 * The advantage of the disign with the condition variables is that it
 * is simple to understand and verify. Threads are only created by the
 * server process, each thread in exactly one place. A silence command
 * resets everything to a well defined state. The audio device is only
 * accessed by the wave thread which seems to alleviate some issues
 * with ALSA.
 * 
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>	/* tmp. for dbg. */
#include <assert.h>
#include <math.h>


#ifndef STANDALONE
#include <dlfcn.h>
#endif
#include "flite.h"
#include "flite_version.h"

#include "synthesizer.h"
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

/* Wave thread specific variables */
static pthread_t wave_thread;
static int wave_thread_active;
static pthread_mutex_t wave_mutex;
static pthread_cond_t wave_condition;
/* Wave buffer */
static AUDIO_COMMAND *ac;
static int ac_size;
static int ac_head, ac_tail;
static int ac_synthpos;	/* largest index to play + 1 */
static float time_left = 0;
/* number of seconds of wave data in wave buffer */

/* Text thread and data structures */
static pthread_t text_thread;
static int text_thread_active;
static int text_thread_cancel;
static pthread_mutex_t text_mutex;
static pthread_cond_t text_condition;
/* Text buffer */
static char *text;
static int text_size;
static int text_head, text_tail;
static int text_synthpos;	/* pointer to beyond last piece to be played */

cst_audiodev *audiodev = NULL;	/* Sound device */
static pthread_attr_t ta;	/* for creating threads */
static pthread_mutexattr_t mt_attr; /* For creating mutexes */

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

/* Functions to reset buffers to initial state */
static inline void reset_text_buffer(void)
{
  text_head = text_tail = 0;
  text_synthpos = 0;
  text[0] ='\0';
}

static inline void reset_wave_buffer(void)
{
  ac_head = ac_tail = ac_synthpos = 0;
  time_left = 0;
  /* memset(ac, '\0', ac_size * sizeof(AUDIO_COMMAND));*/
}

/* Function to get current time as a double */
#ifdef DEBUG
#include <sys/time.h>
static inline double get_ticks_count(boid)
{
  struct timeval now;
  double  ticks;

  gettimeofday(&now, NULL);
  ticks=now.tv_sec+((double)now.tv_usec) /1.e6;
  return ticks;
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
  fprintf(stderr, "Got a seg fault -- exiting.\n");
  unlink(sockname);
  _exit(11);
}
#endif


#define ES_LOG_STATE(label) \
  es_log(2, "%s: %s. ac_head=%d ac_synthpos=%d ac_tail=%d text_head=%d text_synthpos=%d text_tail=%d time_left=%.2f", __func__, label, \
    ac_head, ac_synthpos, ac_tail, text_head, text_synthpos, text_tail, time_left);

/* Helper functions and macros for managing mutexes */
#ifndef DEBUG
#define MUTEX_LOCK(mutex) pthread_mutex_lock(&mutex);
#define MUTEX_UNLOCK(mutex) pthread_mutex_unlock(&mutex);
#else
#define MUTEX_LOCK(mutex) \
  if (pthread_mutex_lock(&mutex) != 0) \
{\
  es_log(2, "%s: Error locking mutex.", __func__); \
  exit(3); \
}

#define MUTEX_UNLOCK(mutex) \
  if (pthread_mutex_unlock(&mutex) != 0) \
{\
  es_log(2, "%s: error unlocking mutex.", __func__); \
  exit(3); \
}
#endif

static void wave_unlock(void *function_name)
{
  es_log(2, "%s: unlocking wave mutex", function_name);
  MUTEX_UNLOCK(wave_mutex);
  es_log(2, "%s: unlocked wave mutex", (char *) function_name);
}

#define WAVE_UNLOCK \
  es_log(2, "%s: unlocking wave mutex", __func__); \
  MUTEX_UNLOCK(wave_mutex); \
  pthread_cleanup_pop(0); \
  es_log(2, "%s: unlocked wave mutex", __func__);

#define WAVE_LOCK \
  es_log(2, "%s: locking wave mutex", __func__); \
  pthread_cleanup_push(wave_unlock, (void *) __func__); \
  MUTEX_LOCK(wave_mutex); \
  es_log(2, "%s: got wave mutex", __func__); \
  pthread_testcancel();


static void text_unlock(void *function_name)
{
  es_log(2, "%s: unlocking text mutex", (char *) function_name);
  MUTEX_UNLOCK(text_mutex);
  es_log(2, "%s: unlocked text mutex", (char *) function_name);
}

#define TEXT_UNLOCK \
  es_log(2, "%s: unlocking text mutex", __func__); \
  MUTEX_UNLOCK(text_mutex); \
  pthread_cleanup_pop(0);\
  es_log(2, "%s: unlocked text mutex", __func__);

#define TEXT_LOCK \
  es_log(2, "%s: locking text mutex", __func__); \
  pthread_cleanup_push(text_unlock, (void *) __func__); \
  MUTEX_LOCK(text_mutex); \
  es_log(2, "%s: got text mutex", __func__); \
  pthread_testcancel();

#define TEXT_LOCK_NI \
  es_log(2, "%s: locking text mutex", __func__); \
  MUTEX_LOCK(text_mutex); \
  es_log(2, "%s: got text mutex", __func__);

#define TEXT_UNLOCK_NI \
  MUTEX_UNLOCK(text_mutex); \
  es_log(2, "%s: unlocked text mutex", __func__);


#define WAVE_LOCK_NI \
  es_log(2, "%s: locking wave mutex", __func__); \
  MUTEX_LOCK(wave_mutex); \
  es_log(2, "%s: got wave mutex", __func__);

#define WAVE_UNLOCK_NI \
  MUTEX_UNLOCK(wave_mutex); \
  es_log(2, "%s: unlocked wave mutex", __func__);


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
	/* We want our threads to be joinable */
	pthread_attr_init(&ta);
	pthread_mutexattr_init(&mt_attr);
#ifdef DEBUG
	pthread_mutexattr_settype(&mt_attr, PTHREAD_MUTEX_ERRORCHECK_NP);
#endif
	pthread_mutex_init(&text_mutex, &mt_attr);
	pthread_cond_init(&text_condition, NULL);
	text_thread_active = 0;
	text_thread_cancel = 0;
	text_size = 4096;
	text = (char *)malloc(text_size);
	text_head = text_tail = text_synthpos = 0;
	wave_thread_active = 0;
	ac_size = 64;
	ac = (AUDIO_COMMAND *)malloc(ac_size * sizeof(AUDIO_COMMAND));
	ac_head = ac_tail = ac_synthpos = 0;
	if (!ac || !text) return NULL;
	pthread_mutex_init(&wave_mutex, &mt_attr);
	pthread_cond_init(&wave_condition, NULL);
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
  if (ref_count == 0)
  {
	int ret;
	/* Wait for any speech to be spoken */
	if (text_thread_active)
	{
	  while (text_tail > 0)
		usleep(100000);
	  TEXT_LOCK_NI;
	  text_thread_cancel = 1;
	  pthread_cond_signal(&text_condition);
	  TEXT_UNLOCK_NI;
	  ret = pthread_join(text_thread, NULL);
	  assert(ret == 0);
	}
	if (wave_thread_active)
	{
	  while(ac_tail >0)
		usleep(100000);
	  pthread_cancel(wave_thread);
	  ret = pthread_join(wave_thread, NULL);
	  assert(ret == 0);
	}
	  
	if (text) free(text);
	if (ac) free(ac);
	text =NULL;
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



static void play_audio_close(void *cancel)
{
  if (audiodev)
  {
	audio_drain(audiodev);
	close_audiodev();
	//	usleep(5000);
  }
}


static inline void determine_playlen(int speed, cst_wave *wptr, int type, int *pl, int *s)
{
  int playlen, skip;
  if (type == SPEECH)
  {
	skip = 1500000 / speed;
	playlen = wptr->num_samples - (skip * 2);
	if (playlen > 0 && playlen < 500) playlen += (skip * 2) / 3;
  }
  else
  {
	skip = 0;
	playlen = wptr->num_samples;
  }
  if (playlen < 0) playlen = 0;

  *pl = playlen;
  *s = skip;
}

static void * play(void *s)
{
  int playlen;
  int skip;
  cst_wave *wptr;
  int type;
  int *sparam = ((synth_t *)s)->state->param;
#ifdef DEBUG
  double start_time;
#endif

  ES_LOG_STATE("Entering main loop");
  while (1)
  {
	es_log(2, "play: beginning of main loop");
	WAVE_LOCK;
	/* Wait for new wave data to arrive */
  	ES_LOG_STATE("checking condition");
	while (ac_head >= ac_synthpos)
	{
	  es_log(2, "play: Going to sleep.");
	  pthread_cond_wait(&wave_condition, &wave_mutex);
	  ES_LOG_STATE("Woke up, checking condition");
	}
	es_log(2, "play: condition passed.");
	wptr = ac[ac_head].data;
	type = ac[ac_head].type;
	WAVE_UNLOCK;
	pthread_testcancel();
	pthread_cleanup_push(play_audio_close, NULL);

	es_log(2, "Opening audio device.");
	/* We abuse the wave mutex here to avoid being canceled
	 * while the audio device is being openned */
	WAVE_LOCK;
	audiodev = audio_open(wptr->sample_rate, wptr->num_channels, CST_AUDIO_LINEAR16);
	WAVE_UNLOCK;
	if (audiodev == NULL)
	{
	  es_log(2, "Failed to open audio device.");
#ifdef CST_AUDIO_OSS
	  if (errno == -EBUSY)
	  {
		es_log(2, "Device was busy, trying again later..");
		usleep(1000000;
		continue;
	  }
#endif
es_log(2, "Cannot recover, exiting...");
	  exit(1);
	}
	determine_playlen(sparam[S_SPEED], wptr, type, &playlen, &skip);
    es_log(2, "play: wave=%p, samples=%p, num_samples=%d skip=%d playlen=%d", wptr, wptr->samples, wptr->num_samples, skip, playlen);
    if (playlen > 0)
    {
      if (sparam[S_VOLUME] != 1000)
		cst_wave_rescale(wptr, (sparam[S_VOLUME] << 16) / 1000);
      pthread_testcancel();
	  es_log(2, "play: Writing to audio device.");
#ifdef DEBUG
	  start_time = get_ticks_count();
#endif
	  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
      audio_write(audiodev, wptr->samples + skip, playlen * 2);
	  es_log(2, "Write took %.2f seconds.", get_ticks_count() - start_time);
	}
    es_log(2, "play: syncing.");
#ifdef DEBUG
	start_time = get_ticks_count();
#endif
    audio_flush(audiodev);
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	es_log(2, "Flush took %.2f seconds.", get_ticks_count() - start_time);
    es_log(2, "play: Closing audio device");
	close_audiodev();
	pthread_cleanup_pop(0)
	  pthread_testcancel();
	  TEXT_LOCK;
    time_left -= ((float)playlen) / wptr->sample_rate;
	pthread_cond_signal(&text_condition);
	TEXT_UNLOCK;

	WAVE_LOCK;
    ac_destroy(&ac[ac_head]);
	ac_head++;
	if (ac_head == ac_tail)
	{
	  reset_wave_buffer();
	}
    else if (ac_head > (ac_size >> 1))
    {
      es_log(1, "play: compacting wave pointersac_head=%d ac_synthpos=%d ac_tail=%d ac_size=%d", ac_head,ac_synthpos,  ac_tail, ac_size);
      memmove(ac, ac + ac_head, (ac_tail - ac_head) * sizeof(AUDIO_COMMAND));
	  /* The following line is not really necessary */
	  /* memset(ac + ac_tail, '\0', (ac_size - ac_tail) * sizeof(AUDIO_COMMAND));*/
      ac_tail -= ac_head;
      if (ac_synthpos > 0) ac_synthpos -= ac_head;
      ac_head = 0;
    }
	WAVE_UNLOCK;
  	ES_LOG_STATE("After playing");
  }
}

/* Pause speech synthesis if there is more that */
/* 30 seconds of speech in the wave buffer */
#define MAX_WAVE_BUFFER_TIME 30

/* This function assumes to hold the text_mutex when called */
static inline void text_thread_testcancel()
{
  if (text_thread_cancel)
  {
	MUTEX_UNLOCK(text_mutex);
	text_thread_cancel = 0;
	es_log(2, "Text thread cancelled, exiting.");
	pthread_exit(0);
  }
}

static void * synthesize(void * s)
{
  cst_wave *wptr = NULL;
  int command;
  int *sparam = ((synth_t *)s)->state->param;

  ES_LOG_STATE("entering main loop");
  while (1)
  {
	int playlen, skip;

	es_log(2, "synthesize: Beginning of main loop.");
	TEXT_LOCK_NI;
	/* The idea here is to wait until new text data has
	 * arrived and until there is less than MAX_WAVVE_BUFFER_TIME
	 * of wave data in the wave buffer. Only consider time_left
	 * if wave queue contains at least five junks of data.
	*/
	ES_LOG_STATE("checking condition");
	while (!text[text_head] || (time_left > MAX_WAVE_BUFFER_TIME && ac_tail - ac_head > 5))
	{
	  text_thread_testcancel();
	  es_log(2, "synthesize: waiting for new text data. Going to sleep.");
	  pthread_cond_wait(&text_condition, &text_mutex);
	ES_LOG_STATE("Woke up, checking condition");
	}
	ES_LOG_STATE("Condition passed");
	text_thread_testcancel();
	/* Copy command into temporary buffer
	 * so we can release the text mutex whilel
	 * synthesizing the speech */
	size_t command_length = strnlen(text + text_head, text_tail - text_head);
	assert(command_length < text_tail - text_head);
	char buf[command_length + 1];
	strcpy(buf, text + text_head);

	TEXT_UNLOCK_NI;

	switch ((command = buf[0]))
	{
	case 1:	/* text */
	  wptr = flite_text_to_wave(buf + 1, v);
	  break;
	case 2:	/* tone */
	  {
		int freq, dur, vol;
		if (sscanf(buf + 1, "%d %d %d", &freq, &dur, &vol) != 3)
		{
		  es_log(1, "unable to scan tone: %s", text + text_head);
		  break;
		}
		wptr = generate_tone(freq, dur, vol);
		break;
	  }
	default:
	  /* snafu - I'm getting the hell out of here */
	  es_log(1, "synthesize: internal error: unknown command: %x", command);
	  return NULL;
	}
	
	determine_playlen(sparam[S_SPEED], wptr, command, &playlen, &skip);

	WAVE_LOCK_NI;
	/* Make sure there is space in wave buffer */
	if (ac_size == ac_tail + 1)
	{
	  ac_size <<= 1;
	  ac = (AUDIO_COMMAND *)realloc(ac, ac_size * sizeof(AUDIO_COMMAND));
	  if (!ac)
	  {
		fprintf(stderr, "Out of memory, ac_size=%d\n", ac_size);
		exit(1);
	  }
	}

	/* Add newly created waveform to buffer */
	ac[ac_tail].type = command;
	ac[ac_tail++].data = wptr;
	TEXT_LOCK_NI;
	time_left +=((float) playlen) / wptr->sample_rate;
	if (text_head < text_synthpos)
	{
	  ac_synthpos = ac_tail;
	  es_log(2, "synthesize: Waking up wave thread.");
	  pthread_cond_signal(&wave_condition);
	}
	WAVE_UNLOCK_NI;

	/* Text buffer maintenance */
/* Skip text just synthesized */
	text_head += command_length + 1;
	
	/* Compact text buffer if it seems the right thing to do */
	if (text_head == text_tail)
	{
	  reset_text_buffer();
	}
	else if (text_head > (text_size >> 1))
	{
	  //es_log(1, "synthesizer: compacting buffer");
	  memcpy(text, text + text_head, text_tail - text_head + 1);
	  text_tail -= text_head;
	  if (text_synthpos > 0) text_synthpos -= text_head;
	  text_head = 0;
	}
	TEXT_UNLOCK_NI;
	ES_LOG_STATE("After adding");
  }
  /* We  should never get here...*/
  assert(0);
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
  if (text_thread_active)
  {
	TEXT_LOCK_NI;
  }

  if (text_tail + len + 3 >= text_size)
  {
    text_size <<= 1;
	text_size += len;
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
    es_log(2, "s_synth: creating new text thread");
    text_thread_active = 1;
    pthread_create(&text_thread, &ta, synthesize, s);
  }
  else
  {
	/* Signal to text thread that more data is available */
	ES_LOG_STATE("waking up text thread.");
	pthread_cond_signal(&text_condition);
	TEXT_UNLOCK_NI;
  }
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
  /* XXX */
  if (!text_thread_active) return 0;

  TEXT_LOCK_NI;
  text_synthpos = text_tail;
  TEXT_UNLOCK_NI;

  if (!wave_thread_active)
  {
	es_log(2, "es_flush: creating play thread");
	WAVE_LOCK_NI;
	wave_thread_active = 1;
	ac_synthpos = ac_tail;
	pthread_create(&wave_thread, &ta, play, s);
	WAVE_UNLOCK_NI;
  }
  else
  {
	WAVE_LOCK_NI;
	if (ac_synthpos < ac_tail)
	{
	  ac_synthpos = ac_tail;
	  pthread_cond_signal(&wave_condition);
	}
	WAVE_UNLOCK_NI;
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
  int i, ret;

  es_log(2, "s_clear: text=%d %d, wave=%d %d", text_head, text_tail, ac_head, ac_tail);

    if (wave_thread_active)
  {
	WAVE_LOCK_NI;
	pthread_cancel(wave_thread);
	WAVE_UNLOCK_NI;
  }

  if (text_thread_active)
  {
	TEXT_LOCK_NI;
	text_thread_cancel = 1;
	pthread_cond_signal(&text_condition);
	TEXT_UNLOCK_NI;
  }

  if (text_thread_active)
  {
	ret = pthread_join(text_thread, NULL);
	assert(ret == 0);
  }
  if (wave_thread_active)
  {
	ret =pthread_join(wave_thread, NULL);
	assert(ret == 0);
  }
	
  /* At this point, no thread is running */
  
  /* Free any wave data */
  es_log(2, "s_clear: freeing wave data: %d", ac_tail);
  for (i = 0; i < ac_tail; i++)
  {
	if (ac[i].type != NONE) ac_destroy(&ac[i]);
  }

  /* Reset data structures */
  reset_text_buffer();
  reset_wave_buffer();
  text_thread_active = wave_thread_active = 0;
  text_thread_cancel = 0;

    /* XXX: The following code should not be necessary However, on kernel
   * 2.6, the condition variable would sometimes get corrupted and
   * signaling would thus no longer work. Any ideas on why this
   * happens are or how to fix it are welcome.*/
  ret = pthread_cond_destroy(&wave_condition);
  if (ret)
  {
	es_log(2, "s_clear: Wave Condition error %s", strerror(ret));
	fprintf(stderr, "s_clear: Wave condition corrupted and not recoverable.");
	exit(4);
  }
  pthread_cond_init(&wave_condition, NULL);

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
