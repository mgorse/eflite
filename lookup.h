/*
 * brass - Braille and speech server
 *
 * Copyright (C) 2001 by Roger Butenuth, All rights reserved.
 *
 * This is free software, placed under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation.  Please see the file COPYING for details.
 *
 * $Id: lookup.h,v 1.1 2001/01/07 16:48:31 butenuth Exp $
 */
#ifndef LOOKUP_H
#define LOOKUP_H

typedef char *(*lookup_string_t)(void *context, const char *name);

#endif /* LOOKUP_H */
