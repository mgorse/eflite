/*
 * brass - Braille and speech server
 *
 * Copyright (C) 2001 by Roger Butenuth, All rights reserved.
 *
 * This is free software, placed under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation.  Please see the file COPYING for details.
 *
 * $Id: language.h,v 1.3 2001/01/18 19:17:59 butenuth Exp $
 */
#ifndef _LANGUAGE_H
#define _LANGUAGE_H

#include "synthesizer.h"
#include "lookup.h"

typedef enum {
    L_SPEAK_PUNCTUATION,
    L_MAX
} lang_par_t;

typedef struct lang_state *lang_state_p;

typedef struct lang_struct {
    lang_state_p state;
    lang_descr_t *lang;
    synth_t      *synth;
    char         *s_buf;        /* string to be sent to synthesizer */
    int          s_buf_used;    /* characters, (0-byte NOT included in size) */
    int          s_buf_size;    /* current size of buffer */
    int (*close)(struct lang_struct *l);
    int (*change_synth)(struct lang_struct *l, synth_t *synth);
    int (*speak_string)(struct lang_struct *l, unsigned char *buffer);
    int (*get_param)(struct lang_struct *l, lang_par_t par, int *value);
    int (*set_param)(struct lang_struct *l, lang_par_t par, int value);
} lang_t;

lang_t *language_open(void *context, lookup_string_t lookup);
lang_t *german_open(void *context, lookup_string_t lookup);
lang_t *english_open(void *context, lookup_string_t lookup);

void   init_synth_buffer(lang_t *lang);
void   deinit_synth_buffer(lang_t *lang);
int    add_to_synth_buffer(lang_t *lang, const char *str);
int    flush_synth_buffer(lang_t *lang);

#endif /*  _LANGUAGE_H */
