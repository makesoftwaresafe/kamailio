/**
 * Copyright (C) 2017 Daniel-Constantin Mierla (asipto.com)
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/**
 * this file is generated - do not edit
 */

#ifndef __APP_JSDT_KEMI_EXPORT_H__
#define __APP_JSDT_KEMI_EXPORT_H__

#include "../../core/kemi.h"
#include "duktape.h"

#define SR_KEMI_JSDT_EXPORT_SIZE 1536

typedef struct sr_kemi_jsdt_export
{
	duk_c_function pfunc;
	sr_kemi_t *ket;
} sr_kemi_jsdt_export_t;

sr_kemi_t *sr_kemi_jsdt_export_get(int idx);
duk_c_function sr_kemi_jsdt_export_associate(sr_kemi_t *ket);

#endif
