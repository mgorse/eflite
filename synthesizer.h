/*
 * brass - Braille and speech server
 *
 * Copyright (C) 2001 by Roger Butenuth, All rights reserved.
 *
 * This is free software, placed under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation.  Please see the file COPYING for details.
 *
 * $Id: synthesizer.h,v 1.3 2001/01/07 16:48:31 butenuth Exp $
 */
#ifndef SYNTHESIZER_H
#define SYNTHESIZER_H

#include "lookup.h"

#define LANGUAGES            3

/*
 * Language numbers. Do not change the ordering, some programs rely on
 * this ordering because they index some statically initialized arrays
 * with this language numbers.
 */
#define LANG_BRITISH_ENGLISH 0
#define LANG_GERMAN          1
#define LANG_DUMMY           2


typedef struct {
    int  lang;			/* index (see defines above) */
    char *name;			/* descriptive name */
} lang_descr_t;

extern lang_descr_t languages[LANGUAGES];

typedef enum {
    S_SPEED,			/* 1000 = normal */
    S_PITCH,			/* 1000 = normal */
    S_VOLUME,			/* 1000 = normal */
    S_MAX
} synth_par_t;


typedef struct synth_state *synth_state_p;

typedef struct synth_struct {
    synth_state_p state;
    lang_descr_t  *lang;
    char          *name;
    void          *lib_handle;
    int (*close)(struct synth_struct *s);
    int (*synth)(struct synth_struct *s, unsigned char *buffer);
    int (*flush)(struct synth_struct *s);
    int (*clear)(struct synth_struct *s);
    int (*index_set)(struct synth_struct *s);
    int (*index_wait)(struct synth_struct *s, int id, int timeout);
    int (*get_param)(struct synth_struct *s, synth_par_t par, int *value);
    int (*set_param)(struct synth_struct *s, synth_par_t par, int value);
} synth_t;

synth_t *open_synthesizer(void *context, lookup_string_t lookup);
int     close_synthesizer(synth_t *synth);

/* open function in one of the libraries */
synth_t *synth_open(void *context, lookup_string_t lookup);

#endif /* SYNTHESIZER_H */
