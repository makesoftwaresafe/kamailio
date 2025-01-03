/*
 * siptrace module - helper module to trace sip messages
 *
 * Copyright (C) 2017 kamailio.org
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

#ifndef _SIPTRACE_HEP_H_
#define _SIPTRACE_HEP_H_

#include "../../core/ip_addr.h"

int trace_send_hep_duplicate(
		str *body, str *from, str *to, struct dest_info *, str *correlation_id);
int trace_send_hep2_duplicate(
		str *body, str *from, str *to, struct dest_info *);
int trace_send_hep3_duplicate(
		str *body, str *from, str *to, struct dest_info *, str *correlation_id);
int pipport2su(
		char *pipport, union sockaddr_union *tmp_su, unsigned int *proto);
int hlog(struct sip_msg *msg, str *correlationid, str *message);

#endif
