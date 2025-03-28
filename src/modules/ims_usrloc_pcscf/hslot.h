/*
 * $Id$
 *
 * Copyright (C) 2012 Smile Communications, jason.penton@smilecoms.com
 * Copyright (C) 2012 Smile Communications, richard.good@smilecoms.com
 *
 * The initial version of this code was written by Dragos Vingarzan
 * (dragos(dot)vingarzan(at)fokus(dot)fraunhofer(dot)de and the
 * Fraunhofer FOKUS Institute. It was and still is maintained in a separate
 * branch of the original SER. We are therefore migrating it to
 * Kamailio/SR and look forward to maintaining it from here on out.
 * 2011/2012 Smile Communications, Pty. Ltd.
 * ported/maintained/improved by
 * Jason Penton (jason(dot)penton(at)smilecoms.com and
 * Richard Good (richard(dot)good(at)smilecoms.com) as part of an
 * effort to add full IMS support to Kamailio/SR using a new and
 * improved architecture
 *
 * NB: A lot of this code was originally part of OpenIMSCore,
 * FhG Fokus.
 * Copyright (C) 2004-2006 FhG Fokus
 * Thanks for great work! This is an effort to
 * break apart the various CSCF functions into logically separate
 * components. We hope this will drive wider use. We also feel
 * that in this way the architecture is more complete and thereby easier
 * to manage in the Kamailio/SR environment
 *
 * This file is part of Kamailio, a free SIP server.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Kamailio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * Kamailio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef HSLOT_H
#define HSLOT_H

#include "../../core/locking.h"

#include "udomain.h"
#include "pcontact.h"


struct udomain;
struct pcontact;


typedef struct hslot
{
	int n;					/*!< Number of elements in the collision slot */
	struct pcontact *first; /*!< First element in the list */
	struct pcontact *last;	/*!< Last element in the list */
	struct udomain *d;		/*!< Domain we belong to */
#ifdef GEN_LOCK_T_PREFERED
	gen_lock_t *lock; /*!< Lock for hash entry - fastlock */
#else
	int lockidx; /*!< Lock index for hash entry - the rest*/
#endif
} hslot_t;

void init_slot(struct udomain *_d, hslot_t *_s, int n);
void deinit_slot(hslot_t *_s);
void slot_add(hslot_t *_s, struct pcontact *_r);
void slot_rem(hslot_t *_s, struct pcontact *_r);
int ul_init_locks(void);
void ul_unlock_locks(void);
void ul_destroy_locks(void);

#ifndef GEN_LOCK_T_PREFERED
void ul_lock_idx(int idx);
void ul_release_idx(int idx);
#endif

#endif /* HSLOT_H */
