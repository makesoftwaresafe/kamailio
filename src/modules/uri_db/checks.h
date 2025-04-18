/*
 * Various URI checks
 *
 * Copyright (C) 2001-2004 FhG FOKUS
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


#ifndef CHECKS_H
#define CHECKS_H

#include "../../core/parser/msg_parser.h"


/*
 * Check if To header field contains the same username
 * as digest credentials
 */
int check_to(struct sip_msg *_msg, char *_str1, char *_str2);


/*
 * Check if From header field contains the same username
 * as digest credentials
 */
int check_from(struct sip_msg *_msg, char *_str1, char *_str2);


/*
 * Checks username part of the supplied sip URI.
 * Optional with supplied credentials.
 */
int check_uri(struct sip_msg *msg, char *uri, char *username, char *realm);


/*
 * Check if uri belongs to a local user, contributed by Juha Heinanen
 */
int does_uri_exist(struct sip_msg *_msg, char *_table, char *_s2);


int uridb_db_init(const str *db_url);
int uridb_db_bind(const str *db_url);
void uridb_db_close(void);
int uridb_db_ver(const str *db_url);

int ki_check_to(struct sip_msg *_m);
int ki_check_from(struct sip_msg *_m);
int ki_check_uri(struct sip_msg *msg, str *suri);
int ki_check_uri_realm(
		struct sip_msg *msg, str *suri, str *susername, str *srealm);
int ki_does_uri_exist(struct sip_msg *_msg);

#endif /* CHECKS_H */
