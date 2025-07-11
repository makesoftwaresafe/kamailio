/*
 * Copyright (C) 2007 voice-system.ro
 * Copyright (C) 2009 asipto.com
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

/*! \file
 * \brief Support for transformations
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

#include "../../core/dprint.h"
#include "../../core/mem/mem.h"
#include "../../core/ut.h"
#include "../../core/trim.h"
#include "../../core/pvapi.h"
#include "../../core/dset.h"
#include "../../core/basex.h"
#include "../../core/action.h"
#include "../../core/hashes.h"

#include "../../core/parser/parse_param.h"
#include "../../core/parser/parse_uri.h"
#include "../../core/parser/parse_to.h"
#include "../../core/parser/parse_nameaddr.h"

#include "../../core/strutils.h"
#include "../../core/crypto/shautils.h"
#include "pv_trans.h"


static char _tr_empty_buf[2] = {0};
static str _tr_empty = {_tr_empty_buf, 0};
static str _tr_uri = {0, 0};
static struct sip_uri _tr_parsed_uri;
static param_t *_tr_uri_params = NULL;

/*! transformation buffer size */
#define TR_BUFFER_SIZE 65536
#define TR_BUFFER_SLOTS 16

/*! transformation buffer */
static char **_tr_buffer_list = NULL;

static char *_tr_buffer = NULL;

static int _tr_buffer_idx = 0;

/*!
 *
 */
int tr_init_buffers(void)
{
	int i;

	_tr_buffer_list = (char **)malloc(TR_BUFFER_SLOTS * sizeof(char *));
	if(_tr_buffer_list == NULL)
		return -1;
	for(i = 0; i < TR_BUFFER_SLOTS; i++) {
		_tr_buffer_list[i] = (char *)malloc(TR_BUFFER_SIZE);
		if(_tr_buffer_list[i] == NULL)
			return -1;
	}
	return 0;
}

/*!
 *
 */
char *tr_set_crt_buffer(void)
{
	_tr_buffer = _tr_buffer_list[_tr_buffer_idx];
	_tr_buffer_idx = (_tr_buffer_idx + 1) % TR_BUFFER_SLOTS;
	return _tr_buffer;
}

#define tr_string_clone_result                                                \
	do {                                                                      \
		if(val->rs.len > TR_BUFFER_SIZE - 1) {                                \
			LM_ERR("result is too big (cfg line: %d)\n", get_cfg_crt_line()); \
			return -1;                                                        \
		}                                                                     \
		strncpy(_tr_buffer, val->rs.s, val->rs.len);                          \
		val->rs.s = _tr_buffer;                                               \
	} while(0);

/* -- helper functions */

/* Encode 7BIT PDU */
static int pdu_7bit_encode(str sin)
{
	int i, j;
	unsigned char hex;
	unsigned char nleft;
	unsigned char fill;
	char HexTbl[] = {"0123456789ABCDEF"};

	nleft = 1;
	j = 0;
	for(i = 0; i < sin.len; i++) {
		hex = (unsigned char)(*(sin.s)) >> (nleft - 1);
		fill = *(sin.s + 1) << (8 - nleft);
		hex = hex | fill;
		_tr_buffer[j++] = HexTbl[hex >> 4];
		_tr_buffer[j++] = HexTbl[hex & 0x0F];
		nleft++;
		if(nleft == 8) {
			sin.s++;
			i++;
			nleft = 1;
		}
		sin.s++;
	}
	_tr_buffer[j] = '\0';
	return j;
}

/* Decode 7BIT PDU */
static int pdu_7bit_decode(str sin)
{
	int i, j;
	unsigned char nleft = 1;
	unsigned char fill = 0;
	unsigned char oldfill = 0;
	j = 0;
	for(i = 0; i < sin.len; i += 2) {
		_tr_buffer[j] = (unsigned char)hex_to_char(sin.s[i]) << 4;
		_tr_buffer[j] |= (unsigned char)hex_to_char(sin.s[i + 1]);
		fill = (unsigned char)_tr_buffer[j];
		fill >>= (8 - nleft);
		_tr_buffer[j] <<= (nleft - 1);
		_tr_buffer[j] &= 0x7F;
		_tr_buffer[j] |= oldfill;
		oldfill = fill;
		j++;
		nleft++;
		if(nleft == 8) {
			_tr_buffer[j++] = oldfill;
			nleft = 1;
			oldfill = 0;
		}
	}
	_tr_buffer[j] = '\0';
	return j;
}

/* Get only the numeric part of string, e.g.
 * 040/123-456 => 040123456 */
static int getNumericValue(str sin)
{
	int i, j = 0;
	for(i = 0; i < sin.len; i++) {
		if(sin.s[i] >= '0' && sin.s[i] <= '9') {
			_tr_buffer[j++] = sin.s[i];
		}
	}
	_tr_buffer[j] = '\0';
	return j;
}

/* -- transformations functions */

/*!
 * \brief Evaluate string transformations
 * \param msg SIP message
 * \param tp transformation
 * \param subtype transformation type
 * \param val pseudo-variable
 * \return 0 on success, -1 on error
 */
int tr_eval_string(
		struct sip_msg *msg, tr_param_t *tp, int subtype, pv_value_t *val)
{
	int i, j, n, m, max;
	char *p, *s;
	char c;
	str st, st2;
	pv_value_t v, w;
	time_t t;
	uint32_t sz1, sz2;
	struct tm tmv;

	if(val == NULL || val->flags & PV_VAL_NULL)
		return -1;

	tr_set_crt_buffer();

	switch(subtype) {
		case TR_S_LEN:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);

			val->flags = PV_TYPE_INT | PV_VAL_INT | PV_VAL_STR;
			val->ri = val->rs.len;
			val->rs.s = int2str(val->ri, &val->rs.len);
			break;
		case TR_S_INT:
			if(!(val->flags & PV_VAL_INT)) {
				if(str2slong(&val->rs, &val->ri) != 0)
					return -1;
			} else {
				if(!(val->flags & PV_VAL_STR))
					val->rs.s = int2str(val->ri, &val->rs.len);
			}

			val->flags = PV_TYPE_INT | PV_VAL_INT | PV_VAL_STR;
			break;
		case TR_S_RMWS:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len >= TR_BUFFER_SIZE - 1)
				return -1;
			j = 0;
			for(i = 0; i < val->rs.len; i++) {
				if(val->rs.s[i] != ' ' && val->rs.s[i] != '\t'
						&& val->rs.s[i] != '\r' && val->rs.s[i] != '\n') {
					_tr_buffer[j] = val->rs.s[i];
					j++;
				}
			}
			_tr_buffer[j] = '\0';
			val->flags = PV_VAL_STR;
			val->ri = 0;
			val->rs.s = _tr_buffer;
			val->rs.len = j;
			break;
		case TR_S_RMHDWS:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len >= TR_BUFFER_SIZE - 1)
				return -1;
			j = 0;
			c = 0;
			for(i = 0; i < val->rs.len; i++) {
				if(val->rs.s[i] == '\r' || val->rs.s[i] == '\n') {
					c = 1;
				} else if(c != 0
						  && (val->rs.s[i] == ' ' || val->rs.s[i] == '\t')) {
					if(c == 1) {
						_tr_buffer[j] = ' ';
						j++;
						c = 2;
					}
				} else {
					_tr_buffer[j] = val->rs.s[i];
					j++;
					c = 0;
				}
			}
			_tr_buffer[j] = '\0';
			val->flags = PV_VAL_STR;
			val->ri = 0;
			val->rs.s = _tr_buffer;
			val->rs.len = j;
			break;
		case TR_S_RMHLWS:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len >= TR_BUFFER_SIZE - 1)
				return -1;
			j = 0;
			c = 0;
			for(i = 0; i < val->rs.len; i++) {
				if(val->rs.s[i] == '\r' || val->rs.s[i] == '\n') {
					c = 1;
				} else if(c != 0
						  && (val->rs.s[i] == ' ' || val->rs.s[i] == '\t')) {
					c = 2;
				} else {
					_tr_buffer[j] = val->rs.s[i];
					j++;
					c = 0;
				}
			}
			_tr_buffer[j] = '\0';
			val->flags = PV_VAL_STR;
			val->ri = 0;
			val->rs.s = _tr_buffer;
			val->rs.len = j;
			break;
		case TR_S_MD5:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);

			compute_md5(_tr_buffer, val->rs.s, val->rs.len);
			_tr_buffer[MD5_LEN] = '\0';
			val->flags = PV_VAL_STR;
			val->ri = 0;
			val->rs.s = _tr_buffer;
			val->rs.len = MD5_LEN;
			break;
		case TR_S_SHA256:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			compute_sha256(_tr_buffer, (u_int8_t *)val->rs.s, val->rs.len);
			_tr_buffer[SHA256_DIGEST_STRING_LENGTH - 1] = '\0';
			val->flags = PV_VAL_STR;
			val->ri = 0;
			val->rs.s = _tr_buffer;
			val->rs.len = SHA256_DIGEST_STRING_LENGTH - 1;
			break;
		case TR_S_SHA384:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			compute_sha384(_tr_buffer, (u_int8_t *)val->rs.s, val->rs.len);
			_tr_buffer[SHA384_DIGEST_STRING_LENGTH - 1] = '\0';
			val->flags = PV_VAL_STR;
			val->ri = 0;
			val->rs.s = _tr_buffer;
			val->rs.len = SHA384_DIGEST_STRING_LENGTH - 1;
			break;
		case TR_S_SHA512:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			compute_sha512(_tr_buffer, (u_int8_t *)val->rs.s, val->rs.len);
			_tr_buffer[SHA512_DIGEST_STRING_LENGTH - 1] = '\0';
			val->flags = PV_VAL_STR;
			val->ri = 0;
			val->rs.s = _tr_buffer;
			val->rs.len = SHA512_DIGEST_STRING_LENGTH - 1;
			break;
		case TR_S_ENCODEHEXA:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len > TR_BUFFER_SIZE / 2 - 1)
				return -1;
			j = 0;
			for(i = 0; i < val->rs.len; i++) {
				_tr_buffer[j++] =
						fourbits2char[(unsigned char)val->rs.s[i] >> 4];
				_tr_buffer[j++] = fourbits2char[val->rs.s[i] & 0xf];
			}
			_tr_buffer[j] = '\0';
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.len = j;
			break;
		case TR_S_DECODEHEXA:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len > TR_BUFFER_SIZE * 2 - 1)
				return -1;
			for(i = 0; i < val->rs.len / 2; i++) {
				if(val->rs.s[2 * i] >= '0' && val->rs.s[2 * i] <= '9')
					_tr_buffer[i] = (val->rs.s[2 * i] - '0') << 4;
				else if(val->rs.s[2 * i] >= 'a' && val->rs.s[2 * i] <= 'f')
					_tr_buffer[i] = (val->rs.s[2 * i] - 'a' + 10) << 4;
				else if(val->rs.s[2 * i] >= 'A' && val->rs.s[2 * i] <= 'F')
					_tr_buffer[i] = (val->rs.s[2 * i] - 'A' + 10) << 4;
				else
					return -1;

				if(val->rs.s[2 * i + 1] >= '0' && val->rs.s[2 * i + 1] <= '9')
					_tr_buffer[i] += val->rs.s[2 * i + 1] - '0';
				else if(val->rs.s[2 * i + 1] >= 'a'
						&& val->rs.s[2 * i + 1] <= 'f')
					_tr_buffer[i] += val->rs.s[2 * i + 1] - 'a' + 10;
				else if(val->rs.s[2 * i + 1] >= 'A'
						&& val->rs.s[2 * i + 1] <= 'F')
					_tr_buffer[i] += val->rs.s[2 * i + 1] - 'A' + 10;
				else
					return -1;
			}
			_tr_buffer[i] = '\0';
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.len = i;
			break;
		case TR_S_ENCODE7BIT:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len > (TR_BUFFER_SIZE * 7 / 8) - 1)
				return -1;
			i = pdu_7bit_encode(val->rs);
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.len = i;
			break;
		case TR_S_DECODE7BIT:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len > TR_BUFFER_SIZE / 2 - 1)
				return -1;
			i = pdu_7bit_decode(val->rs);
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.len = i;
			break;
		case TR_S_NUMERIC:
			if(!(val->flags & PV_VAL_STR))
				return -1;
			if(val->rs.len > TR_BUFFER_SIZE)
				return -1;
			i = getNumericValue(val->rs);
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.len = i;
			break;
		case TR_S_ENCODEBASE58:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			st.len = TR_BUFFER_SIZE - 1;
			st.s = b58_encode(_tr_buffer, &st.len, val->rs.s, val->rs.len);
			if(st.s == NULL)
				return -1;
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs.s = st.s;
			val->rs.len = st.len;
			break;
		case TR_S_DECODEBASE58:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			st.len = TR_BUFFER_SIZE - 1;
			st.s = b58_decode(_tr_buffer, &st.len, val->rs.s, val->rs.len);
			if(st.s == NULL)
				return -1;
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs.s = st.s;
			val->rs.len = st.len;
			break;
		case TR_S_ENCODEBASE64:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			i = base64_enc((unsigned char *)val->rs.s, val->rs.len,
					(unsigned char *)_tr_buffer, TR_BUFFER_SIZE - 1);
			if(i < 0)
				return -1;
			_tr_buffer[i] = '\0';
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.len = i;
			break;
		case TR_S_DECODEBASE64:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			i = base64_dec((unsigned char *)val->rs.s, val->rs.len,
					(unsigned char *)_tr_buffer, TR_BUFFER_SIZE - 1);
			if(i < 0 || (i == 0 && val->rs.len > 0))
				return -1;
			_tr_buffer[i] = '\0';
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.len = i;
			break;
		case TR_S_ENCODEBASE64T:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			i = base64_enc((unsigned char *)val->rs.s, val->rs.len,
					(unsigned char *)_tr_buffer, TR_BUFFER_SIZE - 1);
			if(i < 0)
				return -1;
			if(i > 1 && _tr_buffer[i - 1] == '=') {
				i--;
				if(i > 1 && _tr_buffer[i - 1] == '=') {
					i--;
				}
			}
			_tr_buffer[i] = '\0';
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.len = i;
			break;
		case TR_S_DECODEBASE64T:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len % 4) {
				if(val->rs.len + 4 >= TR_BUFFER_SIZE - 1) {
					LM_ERR("not enough space to insert padding\n");
					return -1;
				}
				memcpy(_tr_buffer, val->rs.s, val->rs.len);
				for(i = 0; i < (4 - (val->rs.len % 4)); i++) {
					_tr_buffer[val->rs.len + i] = '=';
				}
				st.s = _tr_buffer;
				st.len = val->rs.len + (4 - (val->rs.len % 4));
				/* move to next buffer */
				tr_set_crt_buffer();
				i = base64_dec((unsigned char *)st.s, st.len,
						(unsigned char *)_tr_buffer, TR_BUFFER_SIZE - 1);
			} else {
				i = base64_dec((unsigned char *)val->rs.s, val->rs.len,
						(unsigned char *)_tr_buffer, TR_BUFFER_SIZE - 1);
			}
			if(i < 0 || (i == 0 && val->rs.len > 0))
				return -1;
			_tr_buffer[i] = '\0';
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.len = i;
			break;
		case TR_S_ENCODEBASE64URL:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			i = base64url_enc(
					val->rs.s, val->rs.len, _tr_buffer, TR_BUFFER_SIZE - 1);
			if(i < 0)
				return -1;
			_tr_buffer[i] = '\0';
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.len = i;
			break;
		case TR_S_DECODEBASE64URL:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			i = base64url_dec(
					val->rs.s, val->rs.len, _tr_buffer, TR_BUFFER_SIZE - 1);
			if(i < 0 || (i == 0 && val->rs.len > 0))
				return -1;
			_tr_buffer[i] = '\0';
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.len = i;
			break;
		case TR_S_ENCODEBASE64URLT:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			i = base64url_enc(
					val->rs.s, val->rs.len, _tr_buffer, TR_BUFFER_SIZE - 1);
			if(i < 0)
				return -1;
			if(i > 1 && _tr_buffer[i - 1] == '=') {
				i--;
				if(i > 1 && _tr_buffer[i - 1] == '=') {
					i--;
				}
			}
			_tr_buffer[i] = '\0';
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.len = i;
			break;
		case TR_S_DECODEBASE64URLT:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len % 4) {
				if(val->rs.len + 4 >= TR_BUFFER_SIZE - 1) {
					LM_ERR("not enough space to insert padding\n");
					return -1;
				}
				memcpy(_tr_buffer, val->rs.s, val->rs.len);
				for(i = 0; i < (4 - (val->rs.len % 4)); i++) {
					_tr_buffer[val->rs.len + i] = '=';
				}
				st.s = _tr_buffer;
				st.len = val->rs.len + (4 - (val->rs.len % 4));
				/* move to next buffer */
				tr_set_crt_buffer();
				i = base64url_dec(st.s, st.len, _tr_buffer, TR_BUFFER_SIZE - 1);
			} else {
				i = base64url_dec(
						val->rs.s, val->rs.len, _tr_buffer, TR_BUFFER_SIZE - 1);
			}
			if(i < 0 || (i == 0 && val->rs.len > 0))
				return -1;
			_tr_buffer[i] = '\0';
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.len = i;
			break;
		case TR_S_ESCAPECOMMON:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len > TR_BUFFER_SIZE / 2 - 1)
				return -1;
			i = escape_common(_tr_buffer, val->rs.s, val->rs.len);
			_tr_buffer[i] = '\0';
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.len = i;
			break;
		case TR_S_UNESCAPECOMMON:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len > TR_BUFFER_SIZE - 1)
				return -1;
			i = unescape_common(_tr_buffer, val->rs.s, val->rs.len);
			_tr_buffer[i] = '\0';
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.len = i;
			break;
		case TR_S_ESCAPECRLF:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len > TR_BUFFER_SIZE / 2 - 1)
				return -1;
			st.s = _tr_buffer;
			st.len = TR_BUFFER_SIZE;
			if(escape_crlf(&val->rs, &st))
				return -1;
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs = st;
			break;
		case TR_S_UNESCAPECRLF:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len > TR_BUFFER_SIZE - 1)
				return -1;
			st.s = _tr_buffer;
			st.len = TR_BUFFER_SIZE;
			if(unescape_crlf(&val->rs, &st))
				return -1;
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs = st;
			break;
		case TR_S_ESCAPEUSER:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len > TR_BUFFER_SIZE / 2 - 1)
				return -1;
			st.s = _tr_buffer;
			st.len = TR_BUFFER_SIZE;
			if(escape_user(&val->rs, &st))
				return -1;
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs = st;
			break;
		case TR_S_UNESCAPEUSER:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len > TR_BUFFER_SIZE - 1)
				return -1;
			st.s = _tr_buffer;
			st.len = TR_BUFFER_SIZE;
			if(unescape_user(&val->rs, &st))
				return -1;
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs = st;
			break;
		case TR_S_ESCAPEPARAM:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len > TR_BUFFER_SIZE / 2 - 1)
				return -1;
			st.s = _tr_buffer;
			st.len = TR_BUFFER_SIZE;
			if(escape_param(&val->rs, &st) < 0)
				return -1;
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs = st;
			break;
		case TR_S_UNESCAPEPARAM:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len > TR_BUFFER_SIZE - 1)
				return -1;
			st.s = _tr_buffer;
			st.len = TR_BUFFER_SIZE;
			if(unescape_param(&val->rs, &st) < 0)
				return -1;
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs = st;
			break;
		case TR_S_ESCAPECSV:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			st.s = _tr_buffer;
			st.len = TR_BUFFER_SIZE;
			if(escape_csv(&val->rs, &st))
				return -1;
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs = st;
			break;
		case TR_S_SUBSTR:
			if(tp == NULL || tp->next == NULL) {
				LM_ERR("substr invalid parameters (cfg line: %d)\n",
						get_cfg_crt_line());
				return -1;
			}
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(tp->type == TR_PARAM_NUMBER) {
				i = tp->v.n;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_INT))) {
					LM_ERR("substr cannot get p1 (cfg line: %d)\n",
							get_cfg_crt_line());
					return -1;
				}
				i = v.ri;
			}
			if(tp->next->type == TR_PARAM_NUMBER) {
				j = tp->next->v.n;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->next->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_INT))) {
					LM_ERR("substr cannot get p2 (cfg line: %d)\n",
							get_cfg_crt_line());
					return -1;
				}
				j = v.ri;
			}
			LM_DBG("i=%d j=%d\n", i, j);
			if(j < 0) {
				LM_ERR("substr negative offset (cfg line: %d)\n",
						get_cfg_crt_line());
				return -1;
			}
			val->flags = PV_VAL_STR;
			val->ri = 0;
			if(i >= 0) {
				if(i >= val->rs.len) {
					LM_ERR("substr out of range (cfg line: %d)\n",
							get_cfg_crt_line());
					return -1;
				}
				if(i + j >= val->rs.len)
					j = 0;
				if(j == 0) { /* to end */
					val->rs.s += i;
					val->rs.len -= i;
					tr_string_clone_result;
					break;
				}
				val->rs.s += i;
				val->rs.len = j;
				tr_string_clone_result;
				break;
			}
			i = -i;
			if(i > val->rs.len) {
				LM_ERR("substr out of range (cfg line: %d)\n",
						get_cfg_crt_line());
				return -1;
			}
			if(i < j)
				j = 0;
			if(j == 0) { /* to end */
				val->rs.s += val->rs.len - i;
				val->rs.len = i;
				tr_string_clone_result;
				break;
			}
			val->rs.s += val->rs.len - i;
			val->rs.len = j;
			tr_string_clone_result;
			break;

		case TR_S_SELECT:
			if(tp == NULL || tp->next == NULL) {
				LM_ERR("select invalid parameters (cfg line: %d)\n",
						get_cfg_crt_line());
				return -1;
			}
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(tp->type == TR_PARAM_NUMBER) {
				i = tp->v.n;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_INT))) {
					LM_ERR("select cannot get p1 (cfg line: %d)\n",
							get_cfg_crt_line());
					return -1;
				}
				i = v.ri;
			}
			if(tp->next->v.s.len > 1) {
				switch(tp->next->v.s.s[1]) {
					case '\\':
						c = '\\';
						break;
					case 'n':
						c = '\n';
						break;
					case 'r':
						c = '\r';
						break;
					case 't':
						c = '\t';
						break;
					case 's':
						c = ' ';
						break;
					default:
						LM_ERR("invalid select escape char (cfg line: %d)\n",
								get_cfg_crt_line());
						return -1;
				}
			} else {
				c = tp->next->v.s.s[0];
			}
			val->flags = PV_VAL_STR;
			val->ri = 0;
			if(i < 0) {
				s = val->rs.s + val->rs.len - 1;
				p = s;
				i = -i;
				i--;
				while(p >= val->rs.s) {
					if(((c == 1)
							   && (*p == ' ' || *p == '\t' || *p == '\n'
									   || *p == '\r'))
							|| (*p == c)) {
						if(i == 0)
							break;
						s = p - 1;
						i--;
					}
					p--;
				}
				if(i == 0) {
					val->rs.s = p + 1;
					val->rs.len = s - p;
				} else {
					val->rs = _tr_empty;
				}
			} else {
				s = val->rs.s;
				p = s;
				while(p < val->rs.s + val->rs.len) {
					if(((c == 1)
							   && (*p == ' ' || *p == '\t' || *p == '\n'
									   || *p == '\r'))
							|| (*p == c)) {
						if(i == 0)
							break;
						s = p + 1;
						i--;
					}
					p++;
				}
				if(i == 0) {
					val->rs.s = s;
					val->rs.len = p - s;
				} else {
					val->rs = _tr_empty;
				}
			}
			tr_string_clone_result;
			break;

		case TR_S_TOLOWER:
			if(!(val->flags & PV_VAL_STR)) {
				val->rs.s = int2str(val->ri, &val->rs.len);
				val->flags |= PV_VAL_STR;
				break;
			}
			if(val->rs.len > TR_BUFFER_SIZE - 1)
				return -1;
			st.s = _tr_buffer;
			st.len = val->rs.len;
			for(i = 0; i < st.len; i++)
				st.s[i] = (val->rs.s[i] >= 'A' && val->rs.s[i] <= 'Z')
								  ? ('a' + val->rs.s[i] - 'A')
								  : val->rs.s[i];
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs = st;
			break;

		case TR_S_TOUPPER:
			if(!(val->flags & PV_VAL_STR)) {
				val->rs.s = int2str(val->ri, &val->rs.len);
				val->flags |= PV_VAL_STR;
				break;
			}
			if(val->rs.len > TR_BUFFER_SIZE - 1)
				return -1;
			st.s = _tr_buffer;
			st.len = val->rs.len;
			for(i = 0; i < st.len; i++)
				st.s[i] = (val->rs.s[i] >= 'a' && val->rs.s[i] <= 'z')
								  ? ('A' + val->rs.s[i] - 'a')
								  : val->rs.s[i];
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs = st;
			break;

		case TR_S_STRIP:
		case TR_S_STRIPTAIL:
			if(tp == NULL) {
				LM_ERR("strip invalid parameters (cfg line: %d)\n",
						get_cfg_crt_line());
				return -1;
			}
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(tp->type == TR_PARAM_NUMBER) {
				i = tp->v.n;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_INT))) {
					LM_ERR("select cannot get p1 (cfg line: %d)\n",
							get_cfg_crt_line());
					return -1;
				}
				i = v.ri;
			}
			val->flags = PV_VAL_STR;
			val->ri = 0;
			if(i <= 0)
				break;
			if(i >= val->rs.len) {
				_tr_buffer[0] = '\0';
				val->rs.s = _tr_buffer;
				val->rs.len = 0;
				break;
			}
			if(subtype == TR_S_STRIP)
				val->rs.s += i;
			val->rs.len -= i;
			tr_string_clone_result;
			break;


		case TR_S_STRIPTO:
			if(tp == NULL) {
				LM_ERR("stripto invalid parameters (cfg line: %d)\n",
						get_cfg_crt_line());
				return -1;
			}
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);

			if(tp->type == TR_PARAM_STRING) {
				st = tp->v.s;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_STR)) || v.rs.len <= 0) {
					LM_ERR("stripto cannot get p1 (cfg line: %d)\n",
							get_cfg_crt_line());
					return -1;
				}
				st = v.rs;
			}

			val->flags = PV_VAL_STR;
			val->ri = 0;
			for(i = 0; i < val->rs.len; i++) {
				if(val->rs.s[i] == st.s[0])
					break;
			}
			if(i >= val->rs.len) {
				_tr_buffer[0] = '\0';
				val->rs.s = _tr_buffer;
				val->rs.len = 0;
				break;
			}
			val->rs.s += i;
			val->rs.len -= i;
			tr_string_clone_result;
			break;

		case TR_S_PREFIXES:
		case TR_S_PREFIXES_QUOT:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);


			/* Set maximum prefix length */
			max = val->rs.len;
			if(tp != NULL) {
				if(tp->type == TR_PARAM_NUMBER) {
					if(tp->v.n > 0 && tp->v.n < max)
						max = tp->v.n;
				} else {
					if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
							|| (!(v.flags & PV_VAL_INT))) {
						LM_ERR("prefixes cannot get max (cfg line: %d)\n",
								get_cfg_crt_line());
						return -1;
					}
					if(v.ri > 0 && v.ri < max)
						max = v.ri;
				}
			}

			if(max * (max / 2 + (subtype == TR_S_PREFIXES_QUOT ? 1 : 3))
					> TR_BUFFER_SIZE - 1) {
				LM_ERR("prefixes buffer too short (cfg line: %d)\n",
						get_cfg_crt_line());
				return -1;
			}

			j = 0;
			for(i = 1; i <= max; i++) {
				if(subtype == TR_S_PREFIXES_QUOT)
					_tr_buffer[j++] = '\'';
				memcpy(&(_tr_buffer[j]), val->rs.s, i);
				j += i;
				if(subtype == TR_S_PREFIXES_QUOT)
					_tr_buffer[j++] = '\'';
				_tr_buffer[j++] = ',';
			}
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.len = (j > 0) ? (j - 1) : 0;
			break;


		case TR_S_REPLACE:
			if(tp == NULL || tp->next == NULL) {
				LM_ERR("select invalid parameters (cfg line: %d)\n",
						get_cfg_crt_line());
				return -1;
			}
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);

			if(tp->type == TR_PARAM_STRING) {
				st = tp->v.s;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_STR)) || v.rs.len <= 0) {
					LM_ERR("replace cannot get p1 (cfg line: %d)\n",
							get_cfg_crt_line());
					return -1;
				}
				st = v.rs;
			}

			if(tp->next->type == TR_PARAM_STRING) {
				st2 = tp->next->v.s;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->next->v.data, &w) != 0
						|| (!(w.flags & PV_VAL_STR)) || w.rs.len <= 0) {
					LM_ERR("replace cannot get p2 (cfg line: %d)\n",
							get_cfg_crt_line());
					return -1;
				}
				st2 = w.rs;
			}

			val->flags = PV_VAL_STR;
			val->ri = 0;

			i = 0;
			j = 0;
			max = val->rs.len - st.len;
			while(i < val->rs.len && j < TR_BUFFER_SIZE) {
				if(i <= max && val->rs.s[i] == st.s[0]
						&& strncmp(val->rs.s + i, st.s, st.len) == 0) {
					strncpy(_tr_buffer + j, st2.s, st2.len);
					i += st.len;
					j += st2.len;
				} else {
					_tr_buffer[j++] = val->rs.s[i++];
				}
			}
			val->rs.s = _tr_buffer;
			val->rs.len = j;
			break;

		case TR_S_TIMEFORMAT:
			if(tp == NULL) {
				LM_ERR("timeformat invalid parameters (cfg line: %d)\n",
						get_cfg_crt_line());
				return -1;
			}
			if(!(val->flags & PV_VAL_INT)
					&& (str2slong(&val->rs, &val->ri) != 0)) {
				LM_ERR("value is not numeric (cfg line: %d)\n",
						get_cfg_crt_line());
				return -1;
			}
			if(tp->type == TR_PARAM_STRING) {
				st = tp->v.s;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_STR)) || v.rs.len <= 0) {
					LM_ERR("timeformat cannot get p1 (cfg line: %d)\n",
							get_cfg_crt_line());
					return -1;
				}
				st = v.rs;
			}
			s = pkg_malloc(st.len + 1);
			if(s == NULL) {
				LM_ERR("no more pkg memory (cfg line: %d)\n",
						get_cfg_crt_line());
				return -1;
			}
			memcpy(s, st.s, st.len);
			s[st.len] = '\0';
			t = val->ri;
			memset(&tmv, 0, sizeof(struct tm));
			_tr_buffer[0] = '\0';
			localtime_r(&t, &tmv);
			val->rs.len = strftime(_tr_buffer, TR_BUFFER_SIZE - 1, s, &tmv);
			pkg_free(s);
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			break;

		case TR_S_TRIM:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len > TR_BUFFER_SIZE - 2)
				return -1;
			memcpy(_tr_buffer, val->rs.s, val->rs.len);
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			trim(&val->rs);
			val->rs.s[val->rs.len] = '\0';
			break;

		case TR_S_RTRIM:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len > TR_BUFFER_SIZE - 2)
				return -1;
			memcpy(_tr_buffer, val->rs.s, val->rs.len);
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			trim_trailing(&val->rs);
			val->rs.s[val->rs.len] = '\0';
			break;

		case TR_S_LTRIM:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len > TR_BUFFER_SIZE - 2)
				return -1;
			memcpy(_tr_buffer, val->rs.s, val->rs.len);
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			trim_leading(&val->rs);
			val->rs.s[val->rs.len] = '\0';
			break;

		case TR_S_RM:
			if(tp == NULL) {
				LM_ERR("invalid parameters (cfg line: %d)\n",
						get_cfg_crt_line());
				return -1;
			}
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len > TR_BUFFER_SIZE - 2)
				return -1;
			if(tp->type == TR_PARAM_STRING) {
				st = tp->v.s;
				if(memchr(st.s, '\\', st.len)) {
					p = pv_get_buffer();
					if(st.len >= pv_get_buffer_size() - 1)
						return -1;
					j = 0;
					for(i = 0; i < st.len - 1; i++) {
						if(st.s[i] == '\\') {
							switch(st.s[i + 1]) {
								case 'n':
									p[j++] = '\n';
									break;
								case 'r':
									p[j++] = '\r';
									break;
								case 't':
									p[j++] = '\t';
									break;
								case '\\':
									p[j++] = '\\';
									break;
								default:
									p[j++] = st.s[i + 1];
							}
							i++;
						} else {
							p[j++] = st.s[i];
						}
					}
					if(i == st.len - 1)
						p[j++] = st.s[i];
					p[j] = '\0';
					st.s = p;
					st.len = j;
				}
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_STR)) || v.rs.len <= 0) {
					LM_ERR("cannot get parameter value (cfg line: %d)\n",
							get_cfg_crt_line());
					return -1;
				}
				st = v.rs;
			}
			LM_DBG("removing [%.*s](%d) in [%.*s](%d)\n", st.len, st.s, st.len,
					val->rs.len, val->rs.s, val->rs.len);
			val->flags = PV_VAL_STR;
			val->ri = 0;

			i = 0;
			j = 0;
			max = val->rs.len - st.len;
			while(i < val->rs.len && j < TR_BUFFER_SIZE) {
				if(i <= max && val->rs.s[i] == st.s[0]
						&& strncmp(val->rs.s + i, st.s, st.len) == 0) {
					i += st.len;
				} else {
					_tr_buffer[j++] = val->rs.s[i++];
				}
			}
			val->rs.s = _tr_buffer;
			val->rs.s[j] = '\0';
			val->rs.len = j;
			break;

		case TR_S_URLENCODEPARAM:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len > TR_BUFFER_SIZE - 1)
				return -1;
			st.s = _tr_buffer;
			st.len = TR_BUFFER_SIZE;
			if(urlencode(&val->rs, &st) < 0)
				return -1;
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs = st;
			break;

		case TR_S_URLDECODEPARAM:
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(val->rs.len > TR_BUFFER_SIZE - 1)
				return -1;
			st.s = _tr_buffer;
			st.len = TR_BUFFER_SIZE;
			if(urldecode(&val->rs, &st) < 0)
				return -1;
			memset(val, 0, sizeof(pv_value_t));
			val->flags = PV_VAL_STR;
			val->rs = st;
			break;

		case TR_S_COREHASH:
			if(!(val->flags & PV_VAL_STR))
				st.s = int2str(val->ri, &st.len);
			else
				st = val->rs;

			sz1 = 0;
			if(tp != NULL) {
				if(tp->type == TR_PARAM_NUMBER) {
					sz1 = (uint)tp->v.n;
				} else {
					if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
							|| (!(v.flags & PV_VAL_INT))) {
						LM_ERR("corehash cannot get size (cfg line: %d)\n",
								get_cfg_crt_line());
						return -1;
					}
					sz1 = (uint)v.ri;
				}
			}

			sz2 = core_hash(&st, NULL, sz1);

			if((val->rs.s = int2strbuf((unsigned long)sz2, _tr_buffer,
						INT2STR_MAX_LEN, &val->rs.len))
					== NULL) {
				LM_ERR("failed to convert core hash id to string\n");
				return -1;
			}
			val->flags = PV_VAL_STR;
			val->ri = 0;
			break;

		case TR_S_UNQUOTE:
			if(!(val->flags & PV_VAL_STR)) {
				val->rs.s = int2str(val->ri, &val->rs.len);
				break;
			}
			if(val->rs.len < 2) {
				break;
			}
			if(val->rs.len > TR_BUFFER_SIZE - 2) {
				LM_ERR("value too large: %d\n", val->rs.len);
				return -1;
			}
			if((val->rs.s[0] == val->rs.s[val->rs.len - 1])
					&& (val->rs.s[0] == '"' || val->rs.s[0] == '\'')) {
				memcpy(_tr_buffer, val->rs.s + 1, val->rs.len - 2);
				val->rs.len -= 2;
			} else {
				memcpy(_tr_buffer, val->rs.s, val->rs.len);
			}
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.s[val->rs.len] = '\0';
			break;

		case TR_S_UNBRACKET:
			if(!(val->flags & PV_VAL_STR)) {
				val->rs.s = int2str(val->ri, &val->rs.len);
				break;
			}
			if(val->rs.len < 2) {
				break;
			}
			if(val->rs.len > TR_BUFFER_SIZE - 2) {
				LM_ERR("value too large: %d\n", val->rs.len);
				return -1;
			}
			if((val->rs.s[0] == '(' && val->rs.s[val->rs.len - 1] == ')')
					|| (val->rs.s[0] == '['
							&& val->rs.s[val->rs.len - 1] == ']')
					|| (val->rs.s[0] == '{'
							&& val->rs.s[val->rs.len - 1] == '}')
					|| (val->rs.s[0] == '<'
							&& val->rs.s[val->rs.len - 1] == '>')) {
				memcpy(_tr_buffer, val->rs.s + 1, val->rs.len - 2);
				val->rs.len -= 2;
			} else {
				memcpy(_tr_buffer, val->rs.s, val->rs.len);
			}
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.s[val->rs.len] = '\0';
			break;

		case TR_S_COUNT:
			if(tp == NULL) {
				LM_ERR("invalid parameters (cfg line: %d)\n",
						get_cfg_crt_line());
				return -1;
			}
			if(!(val->flags & PV_VAL_STR)) {
				val->rs.s = int2str(val->ri, &val->rs.len);
			}
			if(tp->type == TR_PARAM_STRING) {
				st = tp->v.s;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_STR)) || v.rs.len <= 0) {
					LM_ERR("cannot get parameter value (cfg line: %d)\n",
							get_cfg_crt_line());
					return -1;
				}
				st = v.rs;
			}
			LM_DBG("counting [%.*s](%d) in [%.*s](%d)\n", st.len, st.s, st.len,
					val->rs.len, val->rs.s, val->rs.len);
			j = 0;
			for(i = 0; i < val->rs.len; i++) {
				if(val->rs.s[i] == st.s[0]) {
					j++;
				}
			}
			val->flags = PV_TYPE_INT | PV_VAL_INT | PV_VAL_STR;
			val->ri = j;
			val->rs.s = int2str(j, &val->rs.len);
			break;

		case TR_S_BEFORE:
		case TR_S_RBEFORE:
			if(tp == NULL) {
				LM_ERR("invalid parameters (cfg line: %d)\n",
						get_cfg_crt_line());
				return -1;
			}
			if(!(val->flags & PV_VAL_STR)) {
				val->rs.s = int2str(val->ri, &val->rs.len);
			}
			if(val->rs.len > TR_BUFFER_SIZE - 2) {
				LM_ERR("value too large: %d\n", val->rs.len);
				return -1;
			}
			if(tp->type == TR_PARAM_STRING) {
				st = tp->v.s;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_STR)) || v.rs.len <= 0) {
					LM_ERR("cannot get parameter value (cfg line: %d)\n",
							get_cfg_crt_line());
					return -1;
				}
				st = v.rs;
			}
			if(subtype == TR_S_BEFORE) {
				for(i = 0; i < val->rs.len; i++) {
					if(val->rs.s[i] == st.s[0]) {
						break;
					}
				}
			} else {
				for(i = val->rs.len - 1; i >= 0; i++) {
					if(val->rs.s[i] == st.s[0]) {
						break;
					}
				}
			}

			if(i == 0) {
				_tr_buffer[0] = '\0';
				val->rs.len = 0;
			} else {
				memcpy(_tr_buffer, val->rs.s, i);
				val->rs.len = i;
			}
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.s[val->rs.len] = '\0';
			break;

		case TR_S_AFTER:
		case TR_S_RAFTER:
			if(tp == NULL) {
				LM_ERR("invalid parameters (cfg line: %d)\n",
						get_cfg_crt_line());
				return -1;
			}
			if(!(val->flags & PV_VAL_STR)) {
				val->rs.s = int2str(val->ri, &val->rs.len);
			}
			if(val->rs.len > TR_BUFFER_SIZE - 2) {
				LM_ERR("value too large: %d\n", val->rs.len);
				return -1;
			}
			if(tp->type == TR_PARAM_STRING) {
				st = tp->v.s;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_STR)) || v.rs.len <= 0) {
					LM_ERR("cannot get parameter value (cfg line: %d)\n",
							get_cfg_crt_line());
					return -1;
				}
				st = v.rs;
			}
			if(subtype == TR_S_AFTER) {
				for(i = 0; i < val->rs.len; i++) {
					if(val->rs.s[i] == st.s[0]) {
						break;
					}
				}
			} else {
				for(i = val->rs.len - 1; i >= 0; i++) {
					if(val->rs.s[i] == st.s[0]) {
						break;
					}
				}
			}
			if(i >= val->rs.len - 1) {
				_tr_buffer[0] = '\0';
				val->rs.len = 0;
			} else {
				memcpy(_tr_buffer, val->rs.s + i + 1, val->rs.len - i - 1);
				val->rs.len = val->rs.len - i - 1;
			}
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.s[val->rs.len] = '\0';
			break;

		case TR_S_FMTLINES:
		case TR_S_FMTLINET:
			if(tp == NULL || tp->next == NULL) {
				LM_ERR("fmtline invalid parameters (cfg line: %d)\n",
						get_cfg_crt_line());
				return -1;
			}
			if(!(val->flags & PV_VAL_STR))
				val->rs.s = int2str(val->ri, &val->rs.len);
			if(tp->type == TR_PARAM_NUMBER) {
				n = tp->v.n;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_INT))) {
					LM_ERR("fmtline cannot get p1 (cfg line: %d)\n",
							get_cfg_crt_line());
					return -1;
				}
				n = v.ri;
			}
			if(tp->next->type == TR_PARAM_NUMBER) {
				m = tp->next->v.n;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->next->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_INT))) {
					LM_ERR("fmtline cannot get p2 (cfg line: %d)\n",
							get_cfg_crt_line());
					return -1;
				}
				m = v.ri;
			}
			if(n <= 0 || m < 0) {
				LM_ERR("fmtline with invalid parameters (cfg line: %d)\n",
						get_cfg_crt_line());
				return -1;
			}
			if(m >= val->rs.len) {
				if(val->rs.len > TR_BUFFER_SIZE - 2) {
					LM_ERR("value too large: %d\n", val->rs.len);
					return -1;
				}

				memcpy(_tr_buffer, val->rs.s, val->rs.len);
				val->flags = PV_VAL_STR;
				val->rs.s = _tr_buffer;
				val->rs.s[val->rs.len] = '\0';
			}
			if(val->rs.len + (((val->rs.len) / n) * (2 + m))
					> TR_BUFFER_SIZE - 2) {
				LM_ERR("value too large: %d\n", val->rs.len);
				return -1;
			}

			p = _tr_buffer;
			for(i = 0; i < val->rs.len; i++) {
				if(i != 0 && (i / n) == 0) {
					*p = '\r';
					p++;
					*p = '\n';
					p++;
					for(j = 0; j < m; j++) {
						if(subtype == TR_S_FMTLINES) {
							*p = ' ';
						} else {
							*p = '\t';
						}
						p++;
					}
				}
				*p = val->rs.s[i];
				p++;
			}
			val->flags = PV_VAL_STR;
			val->rs.s = _tr_buffer;
			val->rs.len = p - _tr_buffer;
			val->rs.s[val->rs.len] = '\0';
			break;

		default:
			LM_ERR("unknown subtype %d (cfg line: %d)\n", subtype,
					get_cfg_crt_line());
			return -1;
	}
	return 0;
}


/*!
 * \brief Evaluate URI transformations
 * \param msg SIP message
 * \param tp transformation
 * \param subtype transformation type
 * \param val pseudo-variable
 * \return 0 on success, -1 on error
 */
int tr_eval_uri(
		struct sip_msg *msg, tr_param_t *tp, int subtype, pv_value_t *val)
{
	pv_value_t v;
	str sv;
	param_hooks_t phooks;
	param_t *pit = NULL;
	str sproto;
	int dlen = 0;

	if(val == NULL || (!(val->flags & PV_VAL_STR)) || val->rs.len <= 0)
		return -1;

	if(_tr_uri.len == 0 || _tr_uri.len != val->rs.len
			|| strncmp(_tr_uri.s, val->rs.s, val->rs.len) != 0) {
		if(val->rs.len > _tr_uri.len) {
			if(_tr_uri.s)
				pkg_free(_tr_uri.s);
			_tr_uri.s = (char *)pkg_malloc((val->rs.len + 1) * sizeof(char));
			if(_tr_uri.s == NULL) {
				LM_ERR("no more private memory\n");
				if(_tr_uri_params != NULL) {
					free_params(_tr_uri_params);
					_tr_uri_params = 0;
				}
				memset(&_tr_uri, 0, sizeof(str));
				memset(&_tr_parsed_uri, 0, sizeof(struct sip_uri));
				return -1;
			}
		}
		_tr_uri.len = val->rs.len;
		memcpy(_tr_uri.s, val->rs.s, val->rs.len);
		_tr_uri.s[_tr_uri.len] = '\0';
		/* reset old values */
		memset(&_tr_parsed_uri, 0, sizeof(struct sip_uri));
		if(_tr_uri_params != NULL) {
			free_params(_tr_uri_params);
			_tr_uri_params = 0;
		}
		if(_tr_uri.len > 4 && _tr_uri.s[_tr_uri.len - 1] == ';') {
			dlen = 1;
		}
		/* parse uri -- params only when requested */
		if(parse_uri(_tr_uri.s, _tr_uri.len - dlen, &_tr_parsed_uri) != 0) {
			LM_ERR("invalid uri [%.*s]\n", val->rs.len, val->rs.s);
			if(_tr_uri_params != NULL) {
				free_params(_tr_uri_params);
				_tr_uri_params = 0;
			}
			pkg_free(_tr_uri.s);
			memset(&_tr_uri, 0, sizeof(str));
			memset(&_tr_parsed_uri, 0, sizeof(struct sip_uri));
			return -1;
		}
	}
	memset(val, 0, sizeof(pv_value_t));
	val->flags = PV_VAL_STR;

	switch(subtype) {
		case TR_URI_USER:
			val->rs = (_tr_parsed_uri.user.s) ? _tr_parsed_uri.user : _tr_empty;
			break;
		case TR_URI_HOST:
			val->rs = (_tr_parsed_uri.host.s) ? _tr_parsed_uri.host : _tr_empty;
			break;
		case TR_URI_PASSWD:
			val->rs = (_tr_parsed_uri.passwd.s) ? _tr_parsed_uri.passwd
												: _tr_empty;
			break;
		case TR_URI_DURI:
		case TR_URI_SAOR:
		case TR_URI_SURI:
			if(_tr_uri.len >= TR_BUFFER_SIZE) {
				LM_WARN("uri too long [%.*s] (%d)\n", _tr_uri.len, _tr_uri.s,
						_tr_uri.len);
				val->rs = _tr_empty;
				break;
			}

			tr_set_crt_buffer();
			sv.s = _tr_uri.s;
			sv.len = 0;
			while(sv.len < _tr_uri.len) {
				if(_tr_uri.s[sv.len] == ':') {
					break;
				}
				sv.len++;
			}
			if(_tr_uri.s[sv.len] != ':') {
				LM_WARN("uri schema not found [%.*s] (%d)\n", _tr_uri.len,
						_tr_uri.s, _tr_uri.len);
				val->rs = _tr_empty;
				break;
			}
			sv.len++;
			memcpy(_tr_buffer, sv.s, sv.len);
			sv.s = _tr_buffer;
			if((_tr_parsed_uri.user.len > 0) && (subtype != TR_URI_DURI)) {
				memcpy(sv.s + sv.len, _tr_parsed_uri.user.s,
						_tr_parsed_uri.user.len);
				sv.len += _tr_parsed_uri.user.len;
				sv.s[sv.len] = '@';
				sv.len++;
			}
			if(_tr_parsed_uri.host.len > 0) {
				memcpy(sv.s + sv.len, _tr_parsed_uri.host.s,
						_tr_parsed_uri.host.len);
				sv.len += _tr_parsed_uri.host.len;
			}
			if(subtype != TR_URI_SAOR) {
				if(_tr_parsed_uri.port.len > 0) {
					sv.s[sv.len] = ':';
					sv.len++;
					memcpy(sv.s + sv.len, _tr_parsed_uri.port.s,
							_tr_parsed_uri.port.len);
					sv.len += _tr_parsed_uri.port.len;
				}
				if(_tr_parsed_uri.transport_val.len > 0) {
					memcpy(sv.s + sv.len, ";transport=", 11);
					sv.len += 11;
					memcpy(sv.s + sv.len, _tr_parsed_uri.transport_val.s,
							_tr_parsed_uri.transport_val.len);
					sv.len += _tr_parsed_uri.transport_val.len;
				}
			}
			sv.s[sv.len] = '\0';
			val->rs = sv;
			break;
		case TR_URI_PORT:
			val->flags |= PV_TYPE_INT | PV_VAL_INT;
			val->rs = (_tr_parsed_uri.port.s) ? _tr_parsed_uri.port : _tr_empty;
			val->ri = _tr_parsed_uri.port_no;
			break;
		case TR_URI_PARAMS:
			val->rs = (_tr_parsed_uri.sip_params.s) ? _tr_parsed_uri.sip_params
													: _tr_empty;
			break;
		case TR_URI_PARAM:
			if(tp == NULL) {
				LM_ERR("param invalid parameters\n");
				return -1;
			}
			if(_tr_parsed_uri.sip_params.len <= 0) {
				val->rs = _tr_empty;
				val->flags = PV_VAL_STR;
				val->ri = 0;
				break;
			}

			if(_tr_uri_params == NULL) {
				sv = _tr_parsed_uri.sip_params;
				if(parse_params(&sv, CLASS_ANY, &phooks, &_tr_uri_params) < 0)
					return -1;
			}
			if(tp->type == TR_PARAM_STRING) {
				sv = tp->v.s;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_STR)) || v.rs.len <= 0) {
					LM_ERR("param cannot get p1\n");
					return -1;
				}
				sv = v.rs;
			}
			for(pit = _tr_uri_params; pit; pit = pit->next) {
				if(pit->name.len == sv.len
						&& strncasecmp(pit->name.s, sv.s, sv.len) == 0) {
					val->rs = pit->body;
					goto done;
				}
			}
			val->rs = _tr_empty;
			break;
		case TR_URI_RMPARAM:
			if(tp == NULL) {
				LM_ERR("param invalid parameters\n");
				return -1;
			}
			if(tp->type == TR_PARAM_STRING) {
				sv = tp->v.s;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_STR)) || v.rs.len <= 0) {
					LM_ERR("param cannot get p1\n");
					return -1;
				}
				sv = v.rs;
			}
			tr_set_crt_buffer();
			val->rs.s = _tr_buffer;
			val->rs.len = TR_BUFFER_SIZE;
			val->flags = PV_VAL_STR;
			val->ri = 0;
			if(ksr_uri_remove_param(&_tr_uri, &sv, &val->rs) < 0) {
				val->rs = _tr_empty;
			}
			break;
		case TR_URI_HEADERS:
			val->rs = (_tr_parsed_uri.headers.s) ? _tr_parsed_uri.headers
												 : _tr_empty;
			break;
		case TR_URI_TRANSPORT:
			val->rs = (_tr_parsed_uri.transport_val.s)
							  ? _tr_parsed_uri.transport_val
							  : _tr_empty;
			break;
		case TR_URI_TTL:
			val->rs = (_tr_parsed_uri.ttl_val.s) ? _tr_parsed_uri.ttl_val
												 : _tr_empty;
			break;
		case TR_URI_UPARAM:
			val->rs = (_tr_parsed_uri.user_param_val.s)
							  ? _tr_parsed_uri.user_param_val
							  : _tr_empty;
			break;
		case TR_URI_MADDR:
			val->rs = (_tr_parsed_uri.maddr_val.s) ? _tr_parsed_uri.maddr_val
												   : _tr_empty;
			break;
		case TR_URI_METHOD:
			val->rs = (_tr_parsed_uri.method_val.s) ? _tr_parsed_uri.method_val
													: _tr_empty;
			break;
		case TR_URI_LR:
			val->rs = (_tr_parsed_uri.lr_val.s) ? _tr_parsed_uri.lr_val
												: _tr_empty;
			break;
		case TR_URI_R2:
			val->rs = (_tr_parsed_uri.r2_val.s) ? _tr_parsed_uri.r2_val
												: _tr_empty;
			break;
		case TR_URI_SCHEME:
			val->rs.s = _tr_uri.s;
			val->rs.len = 0;
			while(val->rs.len < _tr_uri.len) {
				if(_tr_uri.s[val->rs.len] == ':') {
					break;
				}
				val->rs.len++;
			}
			break;
		case TR_URI_TOSOCKET:
			if(get_valid_proto_string(_tr_parsed_uri.proto, 1, 0, &sproto)
					< 0) {
				LM_WARN("unknown transport protocol\n");
				val->rs = _tr_empty;
				break;
			}
			tr_set_crt_buffer();
			val->rs.len = snprintf(_tr_buffer, TR_BUFFER_SIZE, "%.*s:%.*s:%d",
					sproto.len, sproto.s, _tr_parsed_uri.host.len,
					_tr_parsed_uri.host.s,
					(_tr_parsed_uri.port_no != 0) ? (int)_tr_parsed_uri.port_no
												  : 5060);
			if(val->rs.len <= 0 || val->rs.len >= TR_BUFFER_SIZE) {
				LM_WARN("error converting uri to socket address [%.*s]\n",
						_tr_uri.len, _tr_uri.s);
				val->rs = _tr_empty;
				break;
			}
			val->rs.s = _tr_buffer;
			break;
		default:
			LM_ERR("unknown subtype %d\n", subtype);
			return -1;
	}
done:
	return 0;
}

static str _tr_params_str = {0, 0};
static param_t *_tr_params_list = NULL;
static char _tr_params_separator = ';';


/*!
 * \brief Evaluate parameter transformations
 * \param msg SIP message
 * \param tp transformation
 * \param subtype transformation type
 * \param val pseudo-variable
 * \return 0 on success, -1 on error
 */
int tr_eval_paramlist(
		struct sip_msg *msg, tr_param_t *tp, int subtype, pv_value_t *val)
{
	pv_value_t v;
	pv_value_t vs;
	str sv;
	int n, i;
	char separator = ';';
	param_hooks_t phooks;
	param_t *pit = NULL;

	if(val == NULL || (!(val->flags & PV_VAL_STR)) || val->rs.len <= 0)
		return -1;

	if(tp != NULL) {
		if(subtype == TR_PL_COUNT) {
			if(tp->type != TR_PARAM_STRING) {
				if(pv_get_spec_value(msg, (pv_spec_t *)tp->v.data, &vs) != 0
						|| (!(vs.flags & PV_VAL_STR)) || vs.rs.len <= 0) {
					LM_ERR("value cannot get p1\n");
					return -1;
				}
				separator = vs.rs.s[0];
			} else {
				if(tp->v.s.len != 1)
					return -1;
				separator = tp->v.s.s[0];
			}
		} else if(tp->next != NULL) {
			if(tp->next->type != TR_PARAM_STRING || tp->next->v.s.len != 1)
				return -1;
			separator = tp->next->v.s.s[0];
		}
	}

	if(_tr_params_str.len == 0 || _tr_params_str.len != val->rs.len
			|| strncmp(_tr_params_str.s, val->rs.s, val->rs.len) != 0
			|| _tr_params_separator != separator) {
		_tr_params_separator = separator;

		if(val->rs.len > _tr_params_str.len) {
			if(_tr_params_str.s)
				pkg_free(_tr_params_str.s);
			_tr_params_str.s =
					(char *)pkg_malloc((val->rs.len + 1) * sizeof(char));
			if(_tr_params_str.s == NULL) {
				LM_ERR("no more private memory\n");
				memset(&_tr_params_str, 0, sizeof(str));
				if(_tr_params_list != NULL) {
					free_params(_tr_params_list);
					_tr_params_list = 0;
				}
				return -1;
			}
		}
		_tr_params_str.len = val->rs.len;
		memcpy(_tr_params_str.s, val->rs.s, val->rs.len);
		_tr_params_str.s[_tr_params_str.len] = '\0';

		/* reset old values */
		if(_tr_params_list != NULL) {
			free_params(_tr_params_list);
			_tr_params_list = 0;
		}

		/* parse params */
		sv = _tr_params_str;
		if(sv.len > 1 && sv.s[sv.len - 1] == _tr_params_separator) {
			sv.len--;
		}
		if(parse_params2(&sv, CLASS_ANY, &phooks, &_tr_params_list,
				   _tr_params_separator)
				< 0)
			return -1;
	}

	if(_tr_params_list == NULL)
		return -1;

	memset(val, 0, sizeof(pv_value_t));
	val->flags = PV_VAL_STR;

	switch(subtype) {
		case TR_PL_VALUE:
		case TR_PL_IN:
			if(tp == NULL) {
				LM_ERR("value invalid parameters\n");
				return -1;
			}

			if(tp->type == TR_PARAM_STRING) {
				sv = tp->v.s;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_STR)) || v.rs.len <= 0) {
					LM_ERR("value cannot get p1\n");
					return -1;
				}
				sv = v.rs;
			}

			for(pit = _tr_params_list; pit; pit = pit->next) {
				if(pit->name.len == sv.len
						&& strncasecmp(pit->name.s, sv.s, sv.len) == 0) {
					if(subtype == TR_PL_IN) {
						val->ri = 1;
						val->flags = PV_TYPE_INT | PV_VAL_INT | PV_VAL_STR;
						val->rs.s = int2str(val->ri, &val->rs.len);
					} else {
						val->rs = pit->body;
					}
					goto done;
				}
			}
			if(subtype == TR_PL_IN) {
				val->ri = 0;
				val->flags = PV_TYPE_INT | PV_VAL_INT | PV_VAL_STR;
				val->rs.s = int2str(val->ri, &val->rs.len);
			} else {
				val->rs = _tr_empty;
			}
			break;

		case TR_PL_VALUEAT:
			if(tp == NULL) {
				LM_ERR("name invalid parameters\n");
				return -1;
			}

			if(tp->type == TR_PARAM_NUMBER) {
				n = tp->v.n;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_INT))) {
					LM_ERR("name cannot get p1\n");
					return -1;
				}
				n = v.ri;
			}
			if(n < 0) {
				n = -n;
				n--;
				for(pit = _tr_params_list; pit; pit = pit->next) {
					if(n == 0) {
						val->rs = pit->body;
						goto done;
					}
					n--;
				}
			} else {
				/* ugly hack -- params are in reverse order
				 * - first count then find */
				i = 0;
				for(pit = _tr_params_list; pit; pit = pit->next)
					i++;
				if(n < i) {
					n = i - n - 1;
					for(pit = _tr_params_list; pit; pit = pit->next) {
						if(n == 0) {
							val->rs = pit->body;
							goto done;
						}
						n--;
					}
				}
			}
			val->rs = _tr_empty;
			break;

		case TR_PL_NAME:
			if(tp == NULL) {
				LM_ERR("name invalid parameters\n");
				return -1;
			}

			if(tp->type == TR_PARAM_NUMBER) {
				n = tp->v.n;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_INT))) {
					LM_ERR("name cannot get p1\n");
					return -1;
				}
				n = v.ri;
			}
			if(n < 0) {
				n = -n;
				n--;
				for(pit = _tr_params_list; pit; pit = pit->next) {
					if(n == 0) {
						val->rs = pit->name;
						goto done;
					}
					n--;
				}
			} else {
				/* ugly hack -- params are in reverse order
				 * - first count then find */
				i = 0;
				for(pit = _tr_params_list; pit; pit = pit->next)
					i++;
				if(n < i) {
					n = i - n - 1;
					for(pit = _tr_params_list; pit; pit = pit->next) {
						if(n == 0) {
							val->rs = pit->name;
							goto done;
						}
						n--;
					}
				}
			}
			val->rs = _tr_empty;
			break;

		case TR_PL_COUNT:
			val->ri = 0;
			for(pit = _tr_params_list; pit; pit = pit->next)
				val->ri++;
			val->flags = PV_TYPE_INT | PV_VAL_INT | PV_VAL_STR;
			val->rs.s = int2str(val->ri, &val->rs.len);
			break;

		default:
			LM_ERR("unknown subtype %d\n", subtype);
			return -1;
	}
done:
	return 0;
}

static str _tr_nameaddr_str = {0, 0};
static name_addr_t _tr_nameaddr;


/*!
 * \brief Evaluate name-address transformations
 * \param msg SIP message
 * \param tp transformation
 * \param subtype transformation type
 * \param val pseudo-variable
 * \return 0 on success, -1 on error
 */
int tr_eval_nameaddr(
		struct sip_msg *msg, tr_param_t *tp, int subtype, pv_value_t *val)
{
	str sv;
	int ret;

	if(val == NULL || (!(val->flags & PV_VAL_STR)) || val->rs.len <= 0)
		return -1;

	if(_tr_nameaddr_str.len == 0 || _tr_nameaddr_str.len != val->rs.len
			|| strncmp(_tr_nameaddr_str.s, val->rs.s, val->rs.len) != 0) {
		if(val->rs.len > _tr_nameaddr_str.len) {
			if(_tr_nameaddr_str.s)
				pkg_free(_tr_nameaddr_str.s);
			_tr_nameaddr_str.s =
					(char *)pkg_malloc((val->rs.len + 1) * sizeof(char));

			if(_tr_nameaddr_str.s == NULL) {
				LM_ERR("no more private memory\n");
				memset(&_tr_nameaddr_str, 0, sizeof(str));
				memset(&_tr_nameaddr, 0, sizeof(name_addr_t));
				return -1;
			}
		}
		_tr_nameaddr_str.len = val->rs.len;
		memcpy(_tr_nameaddr_str.s, val->rs.s, val->rs.len);
		_tr_nameaddr_str.s[_tr_nameaddr_str.len] = '\0';

		/* reset old values */
		memset(&_tr_nameaddr, 0, sizeof(name_addr_t));

		/* parse params */
		sv = _tr_nameaddr_str;
		ret = parse_nameaddr(&sv, &_tr_nameaddr);
		if(ret < 0) {
			if(ret != -3)
				return -1;
			/* -3 means no "<" found so treat whole nameaddr as an URI */
			_tr_nameaddr.uri = _tr_nameaddr_str;
			_tr_nameaddr.name = _tr_empty;
			_tr_nameaddr.len = _tr_nameaddr_str.len;
		}
	}

	memset(val, 0, sizeof(pv_value_t));
	val->flags = PV_VAL_STR;

	switch(subtype) {
		case TR_NA_URI:
			val->rs = (_tr_nameaddr.uri.s) ? _tr_nameaddr.uri : _tr_empty;
			break;
		case TR_NA_LEN:
			val->flags = PV_TYPE_INT | PV_VAL_INT | PV_VAL_STR;
			val->ri = _tr_nameaddr.len;
			val->rs.s = int2str(val->ri, &val->rs.len);
			break;
		case TR_NA_NAME:
			val->rs = (_tr_nameaddr.name.s) ? _tr_nameaddr.name : _tr_empty;
			break;

		default:
			LM_ERR("unknown subtype %d\n", subtype);
			return -1;
	}
	return 0;
}

static str _tr_tobody_str = {0, 0};
static struct to_body _tr_tobody = {0};

/*!
 * \brief Evaluate To-Body transformations
 * \param msg SIP message
 * \param tp transformation
 * \param subtype transformation type
 * \param val pseudo-variable
 * \return 0 on success, -1 on error
 */
int tr_eval_tobody(
		struct sip_msg *msg, tr_param_t *tp, int subtype, pv_value_t *val)
{
	str sv;

	if(val == NULL || (!(val->flags & PV_VAL_STR)) || val->rs.len <= 0)
		return -1;

	if(_tr_tobody_str.len == 0 || _tr_tobody_str.len != val->rs.len
			|| strncmp(_tr_tobody_str.s, val->rs.s, val->rs.len) != 0) {
		if(_tr_tobody_str.s == NULL || val->rs.len > _tr_tobody_str.len) {
			if(_tr_tobody_str.s)
				pkg_free(_tr_tobody_str.s);
			_tr_tobody_str.s =
					(char *)pkg_malloc((val->rs.len + 3) * sizeof(char));

			if(_tr_tobody_str.s == NULL) {
				LM_ERR("no more private memory\n");
				free_to_params(&_tr_tobody);
				memset(&_tr_tobody, 0, sizeof(struct to_body));
				memset(&_tr_tobody_str, 0, sizeof(str));
				return -1;
			}
		}
		_tr_tobody_str.len = val->rs.len;
		memcpy(_tr_tobody_str.s, val->rs.s, val->rs.len);
		_tr_tobody_str.s[_tr_tobody_str.len] = '\r';
		_tr_tobody_str.s[_tr_tobody_str.len + 1] = '\n';
		_tr_tobody_str.s[_tr_tobody_str.len + 2] = '\0';

		/* reset old values */
		free_to_params(&_tr_tobody);
		memset(&_tr_tobody, 0, sizeof(struct to_body));

		/* parse params */
		sv = _tr_tobody_str;
		parse_to(sv.s, sv.s + sv.len + 2, &_tr_tobody);
		if(_tr_tobody.error == PARSE_ERROR) {
			free_to_params(&_tr_tobody);
			memset(&_tr_tobody, 0, sizeof(struct to_body));
			pkg_free(_tr_tobody_str.s);
			memset(&_tr_tobody_str, 0, sizeof(str));
			return -1;
		}
		if(parse_uri(
				   _tr_tobody.uri.s, _tr_tobody.uri.len, &_tr_tobody.parsed_uri)
				< 0) {
			free_to_params(&_tr_tobody);
			memset(&_tr_tobody, 0, sizeof(struct to_body));
			pkg_free(_tr_tobody_str.s);
			memset(&_tr_tobody_str, 0, sizeof(str));
			return -1;
		}
	}

	memset(val, 0, sizeof(pv_value_t));
	val->flags = PV_VAL_STR;

	switch(subtype) {
		case TR_TOBODY_URI:
			val->rs = (_tr_tobody.uri.s) ? _tr_tobody.uri : _tr_empty;
			break;
		case TR_TOBODY_TAG:
			val->rs =
					(_tr_tobody.tag_value.s) ? _tr_tobody.tag_value : _tr_empty;
			break;
		case TR_TOBODY_DISPLAY:
			val->rs = (_tr_tobody.display.s) ? _tr_tobody.display : _tr_empty;
			break;
		case TR_TOBODY_URI_USER:
			val->rs = (_tr_tobody.parsed_uri.user.s)
							  ? _tr_tobody.parsed_uri.user
							  : _tr_empty;
			break;
		case TR_TOBODY_URI_HOST:
			val->rs = (_tr_tobody.parsed_uri.host.s)
							  ? _tr_tobody.parsed_uri.host
							  : _tr_empty;
			break;
		case TR_TOBODY_PARAMS:
			if(_tr_tobody.param_lst != NULL) {
				val->rs.s = _tr_tobody.param_lst->name.s;
				val->rs.len = _tr_tobody_str.s + _tr_tobody_str.len - val->rs.s;
			} else
				val->rs = _tr_empty;
			break;

		default:
			LM_ERR("unknown subtype %d\n", subtype);
			return -1;
	}
	return 0;
}

void *memfindrchr(const void *buf, int c, size_t n)
{
	int i;
	unsigned char *p;

	p = (unsigned char *)buf;

	for(i = n - 1; i >= 0; i--) {
		if(p[i] == (unsigned char)c) {
			return (void *)(p + i);
		}
	}
	return NULL;
}

/*!
 * \brief Evaluate line transformations
 * \param msg SIP message
 * \param tp transformation
 * \param subtype transformation type
 * \param val pseudo-variable
 * \return 0 on success, -1 on error
 */
int tr_eval_line(
		struct sip_msg *msg, tr_param_t *tp, int subtype, pv_value_t *val)
{
	pv_value_t v;
	str sv;
	str mv;
	char *p;
	int n, i;

	if(val == NULL || (!(val->flags & PV_VAL_STR)) || val->rs.len <= 0)
		return -1;

	switch(subtype) {
		case TR_LINE_SW:
			if(tp == NULL) {
				LM_ERR("value invalid parameters\n");
				return -1;
			}

			if(tp->type == TR_PARAM_STRING) {
				sv = tp->v.s;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_STR)) || v.rs.len <= 0) {
					LM_ERR("value cannot get p1\n");
					return -1;
				}
				sv = v.rs;
			}

			if(val->rs.len < sv.len) {
				val->rs = _tr_empty;
				goto done;
			}
			p = val->rs.s;
			do {
				if(strncmp(p, sv.s, sv.len) == 0) {
					/* match */
					mv.s = p;
					p += sv.len;
					p = memchr(p, '\n', (val->rs.s + val->rs.len) - p);
					if(p == NULL) {
						/* last line */
						mv.len = (val->rs.s + val->rs.len) - mv.s;
					} else {
						mv.len = p - mv.s;
					}
					val->rs = mv;
					goto done;
				}
				p = memchr(p, '\n', (val->rs.s + val->rs.len) - p);
			} while(p && ((++p) <= val->rs.s + val->rs.len - sv.len));
			val->rs = _tr_empty;
			break;

		case TR_LINE_AT:
			if(tp == NULL) {
				LM_ERR("name invalid parameters\n");
				return -1;
			}

			if(tp->type == TR_PARAM_NUMBER) {
				n = tp->v.n;
			} else {
				if(pv_get_spec_value(msg, (pv_spec_p)tp->v.data, &v) != 0
						|| (!(v.flags & PV_VAL_INT))) {
					LM_ERR("name cannot get p1\n");
					return -1;
				}
				n = v.ri;
			}
			if(n < 0) {
				p = val->rs.s + val->rs.len - 1;
				if(*p == '\n')
					p--;
				mv.s = p;
				n = -n;
				i = 1;
				p = memfindrchr(val->rs.s, '\n', p - val->rs.s);
				if(p != NULL)
					p--;
				while(i < n && p) {
					mv.s = p;
					p = memfindrchr(val->rs.s, '\n', p - val->rs.s);
					if(p != NULL)
						p--;
					i++;
				}
				if(i == n) {
					if(p == NULL) {
						/* first line */
						mv.len = mv.s - val->rs.s + 1;
						mv.s = val->rs.s;
					} else {
						mv.len = mv.s - p - 1;
						mv.s = p + 2;
					}
					val->rs = mv;
					goto done;
				}
			} else {
				p = val->rs.s;
				i = 0;
				while(i < n && p) {
					p = memchr(p, '\n', (val->rs.s + val->rs.len) - p);
					if(p != NULL)
						p++;
					i++;
				}
				if(i == n && p != NULL) {
					/* line found */
					mv.s = p;
					p = memchr(p, '\n', (val->rs.s + val->rs.len) - p);
					if(p == NULL) {
						/* last line */
						mv.len = (val->rs.s + val->rs.len) - mv.s;
					} else {
						mv.len = p - mv.s;
					}
					val->rs = mv;
					goto done;
				}
			}
			val->rs = _tr_empty;
			break;

		case TR_LINE_COUNT:
			n = 0;
			if(val->rs.len > 0) {
				for(i = 0; i < val->rs.len; i++) {
					if(val->rs.s[i] == '\n') {
						n++;
					}
				}
				if(val->rs.s[val->rs.len - 1] != '\n') {
					n++;
				}
			}
			val->flags = PV_TYPE_INT | PV_VAL_INT | PV_VAL_STR;
			val->ri = n;
			val->rs.s = int2str(val->ri, &val->rs.len);
			break;

		default:
			LM_ERR("unknown subtype %d\n", subtype);
			return -1;
	}
done:
	if(val->rs.len > 0) {
		/* skip ending '\r' if present */
		if(val->rs.s[val->rs.len - 1] == '\r')
			val->rs.len--;
	}
	val->flags = PV_VAL_STR;
	return 0;
}

/*!
 * \brief Evaluate urialias transformations
 * \param msg SIP message
 * \param tp transformation
 * \param subtype transformation type
 * \param val pseudo-variable
 * \return 0 on success, -1 on error
 */
int tr_eval_urialias(
		struct sip_msg *msg, tr_param_t *tp, int subtype, pv_value_t *val)
{
	str sv;

	if(val == NULL || (!(val->flags & PV_VAL_STR)) || val->rs.len <= 0)
		return -1;

	switch(subtype) {
		case TR_URIALIAS_ENCODE:
			tr_set_crt_buffer();
			sv.s = _tr_buffer;
			sv.len = TR_BUFFER_SIZE;
			if(ksr_uri_alias_encode(&val->rs, &sv) < 0) {
				LM_WARN("error converting uri to alias [%.*s]\n", val->rs.len,
						val->rs.s);
				val->rs = _tr_empty;
				break;
			}
			val->rs = sv;
			break;
		case TR_URIALIAS_DECODE:
			tr_set_crt_buffer();
			sv.s = _tr_buffer;
			sv.len = TR_BUFFER_SIZE;
			if(ksr_uri_alias_decode(&val->rs, &sv) < 0) {
				LM_WARN("error converting uri to alias [%.*s]\n", val->rs.len,
						val->rs.s);
				val->rs = _tr_empty;
				break;
			}
			val->rs = sv;
			break;

		default:
			LM_ERR("unknown subtype %d\n", subtype);
			return -1;
	}

	val->flags = PV_VAL_STR;
	return 0;
}


/*!
 * \brief Evaluate val transformations
 * \param msg SIP message
 * \param tp transformation
 * \param subtype transformation type
 * \param val pseudo-variable
 * \return 0 on success, -1 on error
 */
int tr_eval_val(
		struct sip_msg *msg, tr_param_t *tp, int subtype, pv_value_t *val)
{
	str sv;
	int emode = 0;

	if(val == NULL)
		return -1;

	switch(subtype) {
		case TR_VAL_N0:
			if(val->flags & PV_VAL_NULL) {
				val->ri = 0;
				tr_set_crt_buffer();
				val->rs.s = _tr_buffer;
				val->rs.s[0] = '0';
				val->rs.s[1] = '\0';
				val->rs.len = 1;
				val->flags = PV_TYPE_INT | PV_VAL_INT | PV_VAL_STR;
			}
			break;
		case TR_VAL_NE:
			if(val->flags & PV_VAL_NULL) {
				val->ri = 0;
				tr_set_crt_buffer();
				val->rs.s = _tr_buffer;
				val->rs.s[0] = '\0';
				val->rs.len = 0;
				val->flags = PV_VAL_STR;
			}
			break;
		case TR_VAL_JSON:
			if(val->flags & PV_VAL_NULL) {
				val->ri = 0;
				tr_set_crt_buffer();
				val->rs.s = _tr_buffer;
				val->rs.s[0] = '\0';
				val->rs.len = 0;
				val->flags = PV_VAL_STR;
			} else if(val->flags & PV_VAL_STR) {
				ksr_str_json_escape(&val->rs, &sv, &emode);
				if(sv.s == NULL) {
					LM_ERR("failed to escape the value\n");
					return -1;
				}
				if(emode == 0) {
					/* no escape was needed */
					return 0;
				}
				if(sv.len >= TR_BUFFER_SIZE - 1) {
					free(sv.s);
					LM_ERR("escaped value is too long\n");
					return -1;
				}
				tr_set_crt_buffer();
				memcpy(_tr_buffer, sv.s, sv.len);
				_tr_buffer[sv.len] = '\0';
				val->rs.s = _tr_buffer;
				val->rs.len = sv.len;
				free(sv.s);
			}
			break;
		case TR_VAL_JSONQE:
			if(val->flags & PV_VAL_NULL) {
				val->ri = 0;
				tr_set_crt_buffer();
				val->rs.s = _tr_buffer;
				val->rs.s[0] = '"';
				val->rs.s[1] = '"';
				val->rs.s[2] = '\0';
				val->rs.len = 2;
				val->flags = PV_VAL_STR;
			} else if(val->flags & PV_TYPE_INT) {
				/* no change needed */
				return 0;
			} else if(val->flags & PV_VAL_STR) {
				ksr_str_json_escape(&val->rs, &sv, &emode);
				if(sv.s == NULL) {
					LM_ERR("failed to escape the value\n");
					return -1;
				}
				if(emode == 0) {
					/* no escape was needed */
					return 0;
				}
				if(sv.len >= TR_BUFFER_SIZE - 3) {
					free(sv.s);
					LM_ERR("escaped value is too long\n");
					return -1;
				}
				tr_set_crt_buffer();
				_tr_buffer[0] = '"';
				memcpy(_tr_buffer + 1, sv.s, sv.len);
				_tr_buffer[sv.len + 1] = '"';
				_tr_buffer[sv.len + 2] = '\0';
				val->rs.s = _tr_buffer;
				val->rs.len = sv.len + 2;
				free(sv.s);
			}
			break;

		default:
			LM_ERR("unknown subtype %d\n", subtype);
			return -1;
	}

	return 0;
}


/*!
 * \brief Evaluate num transformations
 * \param msg SIP message
 * \param tp transformation
 * \param subtype transformation type
 * \param val pseudo-variable
 * \return 0 on success, -1 on error
 */
int tr_eval_num(
		struct sip_msg *msg, tr_param_t *tp, int subtype, pv_value_t *val)
{
	if(val == NULL)
		return -1;

	switch(subtype) {
		case TR_NUM_FDIGIT:
			tr_set_crt_buffer();
			val->rs.s = _tr_buffer;
			if(!(val->flags & PV_VAL_INT)) {
				val->ri = 0;
				val->rs.s[0] = '0';
				val->rs.s[1] = '\0';
				val->rs.len = 1;
				val->flags = PV_TYPE_INT | PV_VAL_INT | PV_VAL_STR;
				return 0;
			}
			if(val->ri < 0) {
				val->ri = -val->ri;
			}
			while(val->ri >= 10) {
				val->ri /= 10;
			}
			val->rs.s[0] = '0' + val->ri;
			val->rs.s[1] = '\0';
			val->rs.len = 1;
			val->flags = PV_TYPE_INT | PV_VAL_INT | PV_VAL_STR;
			break;
		case TR_NUM_LDIGIT:
			tr_set_crt_buffer();
			val->rs.s = _tr_buffer;
			if(!(val->flags & PV_VAL_INT)) {
				val->ri = 0;
				val->rs.s[0] = '0';
				val->rs.s[1] = '\0';
				val->rs.len = 1;
				val->flags = PV_TYPE_INT | PV_VAL_INT | PV_VAL_STR;
				return 0;
			}
			val->ri = val->ri % 10;
			val->rs.s[0] = '0' + val->ri;
			val->rs.s[1] = '\0';
			val->rs.len = 1;
			val->flags = PV_TYPE_INT | PV_VAL_INT | PV_VAL_STR;
			break;

		default:
			LM_ERR("unknown subtype %d\n", subtype);
			return -1;
	}

	return 0;
}


#define _tr_parse_nparam(_p, _p0, _tp, _spec, _n, _sign, _in, _s)              \
	while(is_in_str(_p, _in) && (*_p == ' ' || *_p == '\t' || *_p == '\n'))    \
		_p++;                                                                  \
	if(*_p == PV_MARKER) { /* pseudo-variable */                               \
		_spec = (pv_spec_t *)pkg_malloc(sizeof(pv_spec_t));                    \
		if(_spec == NULL) {                                                    \
			LM_ERR("no more private memory!\n");                               \
			goto error;                                                        \
		}                                                                      \
		_s.s = _p;                                                             \
		_s.len = _in->s + _in->len - _p;                                       \
		_p0 = pv_parse_spec(&_s, _spec);                                       \
		if(_p0 == NULL) {                                                      \
			LM_ERR("invalid spec in substr transformation: %.*s!\n", _in->len, \
					_in->s);                                                   \
			goto error;                                                        \
		}                                                                      \
		_p = _p0;                                                              \
		_tp = (tr_param_t *)pkg_malloc(sizeof(tr_param_t));                    \
		if(_tp == NULL) {                                                      \
			LM_ERR("no more private memory!\n");                               \
			goto error;                                                        \
		}                                                                      \
		memset(_tp, 0, sizeof(tr_param_t));                                    \
		_tp->type = TR_PARAM_SPEC;                                             \
		_tp->v.data = (void *)_spec;                                           \
	} else {                                                                   \
		if(*_p == '+' || *_p == '-'                                            \
				|| (*_p >= '0' && *_p <= '9')) { /* number */                  \
			_sign = 1;                                                         \
			if(*_p == '-') {                                                   \
				_p++;                                                          \
				_sign = -1;                                                    \
			} else if(*_p == '+')                                              \
				_p++;                                                          \
			_n = 0;                                                            \
			while(is_in_str(_p, _in)                                           \
					&& (*_p == ' ' || *_p == '\t' || *_p == '\n'))             \
				_p++;                                                          \
			while(is_in_str(_p, _in) && *_p >= '0' && *_p <= '9') {            \
				if(_n > ((LONG_MAX / 10) - 10)) {                              \
					LM_ERR("number value is too large\n");                     \
					goto error;                                                \
				}                                                              \
				_n = _n * 10 + *_p - '0';                                      \
				_p++;                                                          \
			}                                                                  \
			_tp = (tr_param_t *)pkg_malloc(sizeof(tr_param_t));                \
			if(_tp == NULL) {                                                  \
				LM_ERR("no more private memory!\n");                           \
				goto error;                                                    \
			}                                                                  \
			memset(_tp, 0, sizeof(tr_param_t));                                \
			_tp->type = TR_PARAM_NUMBER;                                       \
			_tp->v.n = n * sign;                                               \
		} else {                                                               \
			LM_ERR("tinvalid param in transformation: %.*s!!\n", _in->len,     \
					_in->s);                                                   \
			goto error;                                                        \
		}                                                                      \
	}

/**
 * _m: 1 - the parameter value can be the TR_PARAM_MARKER; 0 - not allowed
 */
#define _tr_parse_sparamx(_p, _p0, _tp, _spec, _ps, _in, _s, _m)               \
	while(is_in_str(_p, _in) && (*_p == ' ' || *_p == '\t' || *_p == '\n'))    \
		_p++;                                                                  \
	if(*_p == PV_MARKER) { /* pseudo-variable */                               \
		_spec = (pv_spec_t *)pkg_malloc(sizeof(pv_spec_t));                    \
		if(_spec == NULL) {                                                    \
			LM_ERR("no more private memory!\n");                               \
			goto error;                                                        \
		}                                                                      \
		_s.s = _p;                                                             \
		_s.len = _in->s + _in->len - _p;                                       \
		_p0 = pv_parse_spec(&_s, _spec);                                       \
		if(_p0 == NULL) {                                                      \
			LM_ERR("invalid spec in substr transformation: %.*s!\n", _in->len, \
					_in->s);                                                   \
			goto error;                                                        \
		}                                                                      \
		_p = _p0;                                                              \
		_tp = (tr_param_t *)pkg_malloc(sizeof(tr_param_t));                    \
		if(_tp == NULL) {                                                      \
			LM_ERR("no more private memory!\n");                               \
			goto error;                                                        \
		}                                                                      \
		memset(_tp, 0, sizeof(tr_param_t));                                    \
		_tp->type = TR_PARAM_SPEC;                                             \
		_tp->v.data = (void *)_spec;                                           \
	} else { /* string */                                                      \
		_ps = _p;                                                              \
		while(is_in_str(_p, _in) && *_p != '\t' && *_p != '\n'                 \
				&& *_p != TR_PARAM_MARKER && *_p != TR_RBRACKET)               \
			_p++;                                                              \
		if(*_p == '\0') {                                                      \
			LM_ERR("invalid param in transformation: %.*s!!\n", _in->len,      \
					_in->s);                                                   \
			goto error;                                                        \
		}                                                                      \
		if(_m && *_p == TR_PARAM_MARKER) {                                     \
			_p++;                                                              \
			if(*_p == '\0') {                                                  \
				LM_ERR("invalid param in transformation: %.*s!!!\n", _in->len, \
						_in->s);                                               \
				goto error;                                                    \
			}                                                                  \
		}                                                                      \
		_tp = (tr_param_t *)pkg_malloc(sizeof(tr_param_t));                    \
		if(_tp == NULL) {                                                      \
			LM_ERR("no more private memory!!\n");                              \
			goto error;                                                        \
		}                                                                      \
		memset(_tp, 0, sizeof(tr_param_t));                                    \
		_tp->type = TR_PARAM_STRING;                                           \
		_tp->v.s.s = _ps;                                                      \
		_tp->v.s.len = _p - _ps;                                               \
	}

#define _tr_parse_sparam(_p, _p0, _tp, _spec, _ps, _in, _s) \
	_tr_parse_sparamx(_p, _p0, _tp, _spec, _ps, _in, _s, 0)

/*!
 * \brief Helper function to parse a string transformation
 * \param in parsed string
 * \param t transformation
 * \return pointer to the end of the transformation in the string - '}', null on error
 */
char *tr_parse_string(str *in, trans_t *t)
{
	char *p;
	char *p0;
	char *ps;
	str name;
	str s;
	pv_spec_t *spec = NULL;
	long n;
	int sign;
	tr_param_t *tp = NULL;

	if(in == NULL || t == NULL)
		return NULL;

	p = in->s;
	name.s = in->s;
	t->type = TR_STRING;
	t->trf = tr_eval_string;

	/* find next token */
	while(is_in_str(p, in) && *p != TR_PARAM_MARKER && *p != TR_RBRACKET)
		p++;
	if(*p == '\0') {
		LM_ERR("invalid transformation: %.*s\n", in->len, in->s);
		goto error;
	}
	name.len = p - name.s;
	trim(&name);

	if(name.len == 3 && strncasecmp(name.s, "len", 3) == 0) {
		t->subtype = TR_S_LEN;
		goto done;
	} else if(name.len == 3 && strncasecmp(name.s, "int", 3) == 0) {
		t->subtype = TR_S_INT;
		goto done;
	} else if(name.len == 3 && strncasecmp(name.s, "md5", 3) == 0) {
		t->subtype = TR_S_MD5;
		goto done;
	} else if(name.len == 4 && strncasecmp(name.s, "rmws", 4) == 0) {
		t->subtype = TR_S_RMWS;
		goto done;
	} else if(name.len == 6 && strncasecmp(name.s, "rmhdws", 6) == 0) {
		t->subtype = TR_S_RMHDWS;
		goto done;
	} else if(name.len == 6 && strncasecmp(name.s, "rmhlws", 6) == 0) {
		t->subtype = TR_S_RMHLWS;
		goto done;
	} else if(name.len == 6 && strncasecmp(name.s, "sha256", 6) == 0) {
		t->subtype = TR_S_SHA256;
		goto done;
	} else if(name.len == 6 && strncasecmp(name.s, "sha384", 6) == 0) {
		t->subtype = TR_S_SHA384;
		goto done;
	} else if(name.len == 6 && strncasecmp(name.s, "sha512", 6) == 0) {
		t->subtype = TR_S_SHA512;
		goto done;
	} else if(name.len == 7 && strncasecmp(name.s, "tolower", 7) == 0) {
		t->subtype = TR_S_TOLOWER;
		goto done;
	} else if(name.len == 7 && strncasecmp(name.s, "toupper", 7) == 0) {
		t->subtype = TR_S_TOUPPER;
		goto done;
	} else if(name.len == 7 && strncasecmp(name.s, "numeric", 7) == 0) {
		t->subtype = TR_S_NUMERIC;
		goto done;
	} else if(name.len == 11 && strncasecmp(name.s, "encode.hexa", 11) == 0) {
		t->subtype = TR_S_ENCODEHEXA;
		goto done;
	} else if(name.len == 11 && strncasecmp(name.s, "decode.hexa", 11) == 0) {
		t->subtype = TR_S_DECODEHEXA;
		goto done;
	} else if(name.len == 11 && strncasecmp(name.s, "encode.7bit", 11) == 0) {
		t->subtype = TR_S_ENCODE7BIT;
		goto done;
	} else if(name.len == 11 && strncasecmp(name.s, "decode.7bit", 11) == 0) {
		t->subtype = TR_S_DECODE7BIT;
		goto done;
	} else if(name.len == 13 && strncasecmp(name.s, "encode.base58", 13) == 0) {
		t->subtype = TR_S_ENCODEBASE58;
		goto done;
	} else if(name.len == 13 && strncasecmp(name.s, "decode.base58", 13) == 0) {
		t->subtype = TR_S_DECODEBASE58;
		goto done;
	} else if(name.len == 13 && strncasecmp(name.s, "encode.base64", 13) == 0) {
		t->subtype = TR_S_ENCODEBASE64;
		goto done;
	} else if(name.len == 13 && strncasecmp(name.s, "decode.base64", 13) == 0) {
		t->subtype = TR_S_DECODEBASE64;
		goto done;
	} else if(name.len == 14
			  && strncasecmp(name.s, "encode.base64t", 14) == 0) {
		t->subtype = TR_S_ENCODEBASE64T;
		goto done;
	} else if(name.len == 14
			  && strncasecmp(name.s, "decode.base64t", 14) == 0) {
		t->subtype = TR_S_DECODEBASE64T;
		goto done;
	} else if(name.len == 16
			  && strncasecmp(name.s, "encode.base64url", 16) == 0) {
		t->subtype = TR_S_ENCODEBASE64URL;
		goto done;
	} else if(name.len == 16
			  && strncasecmp(name.s, "decode.base64url", 16) == 0) {
		t->subtype = TR_S_DECODEBASE64URL;
		goto done;
	} else if(name.len == 17
			  && strncasecmp(name.s, "encode.base64urlt", 17) == 0) {
		t->subtype = TR_S_ENCODEBASE64URLT;
		goto done;
	} else if(name.len == 17
			  && strncasecmp(name.s, "decode.base64urlt", 17) == 0) {
		t->subtype = TR_S_DECODEBASE64URLT;
		goto done;
	} else if(name.len == 13 && strncasecmp(name.s, "escape.common", 13) == 0) {
		t->subtype = TR_S_ESCAPECOMMON;
		goto done;
	} else if(name.len == 15
			  && strncasecmp(name.s, "unescape.common", 15) == 0) {
		t->subtype = TR_S_UNESCAPECOMMON;
		goto done;
	} else if(name.len == 11 && strncasecmp(name.s, "escape.crlf", 11) == 0) {
		t->subtype = TR_S_ESCAPECRLF;
		goto done;
	} else if(name.len == 13 && strncasecmp(name.s, "unescape.crlf", 13) == 0) {
		t->subtype = TR_S_UNESCAPECRLF;
		goto done;
	} else if(name.len == 11 && strncasecmp(name.s, "escape.user", 11) == 0) {
		t->subtype = TR_S_ESCAPEUSER;
		goto done;
	} else if(name.len == 13 && strncasecmp(name.s, "unescape.user", 13) == 0) {
		t->subtype = TR_S_UNESCAPEUSER;
		goto done;
	} else if(name.len == 12 && strncasecmp(name.s, "escape.param", 12) == 0) {
		t->subtype = TR_S_ESCAPEPARAM;
		goto done;
	} else if(name.len == 14
			  && strncasecmp(name.s, "unescape.param", 14) == 0) {
		t->subtype = TR_S_UNESCAPEPARAM;
		goto done;
	} else if(name.len == 10 && strncasecmp(name.s, "escape.csv", 10) == 0) {
		t->subtype = TR_S_ESCAPECSV;
		goto done;
	} else if(name.len == 8 && strncasecmp(name.s, "prefixes", 8) == 0) {
		t->subtype = TR_S_PREFIXES;
		if(*p != TR_PARAM_MARKER)
			goto done;
		p++;
		_tr_parse_nparam(p, p0, tp, spec, n, sign, in, s);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid prefixes transformation: %.*s!!\n", in->len, in->s);
			goto error;
		}
		goto done;
	} else if(name.len == 15
			  && strncasecmp(name.s, "prefixes.quoted", 15) == 0) {
		t->subtype = TR_S_PREFIXES_QUOT;
		if(*p != TR_PARAM_MARKER)
			goto done;
		p++;
		_tr_parse_nparam(p, p0, tp, spec, n, sign, in, s);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid prefixes transformation: %.*s!!\n", in->len, in->s);
			goto error;
		}
		goto done;
	} else if(name.len == 6 && strncasecmp(name.s, "substr", 6) == 0) {
		t->subtype = TR_S_SUBSTR;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid substr transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_nparam(p, p0, tp, spec, n, sign, in, s);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid substr transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_nparam(p, p0, tp, spec, n, sign, in, s);
		if(tp->type == TR_PARAM_NUMBER && tp->v.n < 0) {
			LM_ERR("substr negative offset\n");
			goto error;
		}
		t->params->next = tp;
		tp = 0;
		while(is_in_str(p, in) && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid substr transformation: %.*s!!\n", in->len, in->s);
			goto error;
		}
		goto done;
	} else if(name.len == 6 && strncasecmp(name.s, "select", 6) == 0) {
		t->subtype = TR_S_SELECT;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid select transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_nparam(p, p0, tp, spec, n, sign, in, s);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_PARAM_MARKER || *(p + 1) == '\0') {
			LM_ERR("invalid select transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		p++;
		tp = (tr_param_t *)pkg_malloc(sizeof(tr_param_t));
		if(tp == NULL) {
			LM_ERR("no more private memory!\n");
			goto error;
		}
		memset(tp, 0, sizeof(tr_param_t));
		tp->type = TR_PARAM_STRING;
		tp->v.s.s = p;
		tp->v.s.len = 1;
		if(*p == '\\') {
			p++;
			if(*p == '\\' || *p == 'n' || *p == 'r' || *p == 't') {
				tp->v.s.len = 2;
			} else {
				LM_ERR("unexpected escape char: %c\n", *p);
				goto error;
			}
		}
		t->params->next = tp;
		tp = 0;
		p++;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid select transformation: %.*s (c:%c/%d - p:%d)!!\n",
					in->len, in->s, *p, *p, (int)(p - in->s));
			goto error;
		}
		goto done;
	} else if(name.len == 5 && strncasecmp(name.s, "strip", 5) == 0) {
		t->subtype = TR_S_STRIP;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid strip transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_nparam(p, p0, tp, spec, n, sign, in, s);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid strip transformation: %.*s!!\n", in->len, in->s);
			goto error;
		}
		goto done;
	} else if(name.len == 9 && strncasecmp(name.s, "striptail", 9) == 0) {
		t->subtype = TR_S_STRIPTAIL;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid striptail transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_nparam(p, p0, tp, spec, n, sign, in, s);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid striptail transformation: %.*s!!\n", in->len,
					in->s);
			goto error;
		}
		goto done;
	} else if(name.len == 7 && strncasecmp(name.s, "stripto", 7) == 0) {
		t->subtype = TR_S_STRIPTO;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid stripto transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_sparam(p, p0, tp, spec, ps, in, s);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid strip transformation: %.*s!!\n", in->len, in->s);
			goto error;
		}
		goto done;
	} else if(name.len == 5 && strncasecmp(name.s, "ftime", 5) == 0) {
		t->subtype = TR_S_TIMEFORMAT;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid ftime transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_sparam(p, p0, tp, spec, ps, in, s);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid ftime transformation: %.*s!!\n", in->len, in->s);
			goto error;
		}
		goto done;
	} else if(name.len == 7 && strncasecmp(name.s, "replace", 7) == 0) {
		t->subtype = TR_S_REPLACE;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid replace transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_sparam(p, p0, tp, spec, ps, in, s);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid replace transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_sparam(p, p0, tp, spec, ps, in, s);
		t->params->next = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid replace transformation: %.*s!!\n", in->len, in->s);
			goto error;
		}
		goto done;
	} else if(name.len == 8 && strncasecmp(name.s, "corehash", 8) == 0) {
		t->subtype = TR_S_COREHASH;
		if(*p == TR_PARAM_MARKER) {
			p++;
			_tr_parse_nparam(p, p0, tp, spec, n, sign, in, s);
			t->params = tp;
		}
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid corehash transformation: %.*s!!\n", in->len, in->s);
			goto error;
		}
		goto done;
	} else if(name.len == 4 && strncasecmp(name.s, "trim", 4) == 0) {
		t->subtype = TR_S_TRIM;
		goto done;
	} else if(name.len == 5 && strncasecmp(name.s, "rtrim", 5) == 0) {
		t->subtype = TR_S_RTRIM;
		goto done;
	} else if(name.len == 5 && strncasecmp(name.s, "ltrim", 5) == 0) {
		t->subtype = TR_S_LTRIM;
		goto done;
	} else if(name.len == 2 && strncasecmp(name.s, "rm", 2) == 0) {
		t->subtype = TR_S_RM;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid ftime transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_sparam(p, p0, tp, spec, ps, in, s);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid rm transformation: %.*s!!\n", in->len, in->s);
			goto error;
		}
		goto done;
	} else if(name.len == 15
			  && strncasecmp(name.s, "urlencode.param", 15) == 0) {
		t->subtype = TR_S_URLENCODEPARAM;
		goto done;
	} else if(name.len == 15
			  && strncasecmp(name.s, "urldecode.param", 15) == 0) {
		t->subtype = TR_S_URLDECODEPARAM;
		goto done;
	} else if(name.len == 7 && strncasecmp(name.s, "unquote", 7) == 0) {
		t->subtype = TR_S_UNQUOTE;
		goto done;
	} else if(name.len == 9 && strncasecmp(name.s, "unbracket", 9) == 0) {
		t->subtype = TR_S_UNBRACKET;
		goto done;
	} else if(name.len == 5 && strncasecmp(name.s, "count", 5) == 0) {
		t->subtype = TR_S_COUNT;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid count transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_sparamx(p, p0, tp, spec, ps, in, s, 1);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid count transformation: %.*s!!\n", in->len, in->s);
			goto error;
		}
		goto done;
	} else if(name.len == 6 && strncasecmp(name.s, "before", 6) == 0) {
		t->subtype = TR_S_BEFORE;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid before transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_sparamx(p, p0, tp, spec, ps, in, s, 1);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid before transformation: %.*s!!\n", in->len, in->s);
			goto error;
		}
		goto done;
	} else if(name.len == 5 && strncasecmp(name.s, "after", 5) == 0) {
		t->subtype = TR_S_AFTER;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid after transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_sparamx(p, p0, tp, spec, ps, in, s, 1);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid after transformation: %.*s!!\n", in->len, in->s);
			goto error;
		}
		goto done;
	} else if(name.len == 7 && strncasecmp(name.s, "rbefore", 7) == 0) {
		t->subtype = TR_S_RBEFORE;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid rbefore transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_sparamx(p, p0, tp, spec, ps, in, s, 1);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid rbefore transformation: %.*s!!\n", in->len, in->s);
			goto error;
		}
		goto done;
	} else if(name.len == 6 && strncasecmp(name.s, "rafter", 6) == 0) {
		t->subtype = TR_S_RAFTER;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid rafter transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_sparamx(p, p0, tp, spec, ps, in, s, 1);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid rafter transformation: %.*s!!\n", in->len, in->s);
			goto error;
		}
		goto done;
	} else if(name.len == 8
			  && (strncasecmp(name.s, "fmtlines", 8) == 0
					  || strncasecmp(name.s, "fmtlinet", 8) == 0)) {
		if(name.s[7] == 's' || name.s[7] == 'S') {
			t->subtype = TR_S_FMTLINES;
		} else {
			t->subtype = TR_S_FMTLINET;
		}
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid fmtline%c transformation: %.*s!\n", name.s[7],
					in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_nparam(p, p0, tp, spec, n, sign, in, s);
		if(tp->type == TR_PARAM_NUMBER && tp->v.n < 0) {
			LM_ERR("fmtline%c negative line length value\n", name.s[7]);
			goto error;
		}

		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid fmtline%c transformation: %.*s!\n", name.s[7],
					in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_nparam(p, p0, tp, spec, n, sign, in, s);
		if(tp->type == TR_PARAM_NUMBER && tp->v.n < 0) {
			LM_ERR("fmtline%c negative padding length value\n", name.s[7]);
			goto error;
		}
		t->params->next = tp;
		tp = 0;
		while(is_in_str(p, in) && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid fmtline%c transformation: %.*s!!\n", name.s[7],
					in->len, in->s);
			goto error;
		}
		goto done;
	}

	LM_ERR("unknown transformation: %.*s/%.*s/%d!\n", in->len, in->s, name.len,
			name.s, name.len);
error:
	if(tp)
		tr_param_free(tp);
	if(spec)
		pv_spec_free(spec);
	return NULL;
done:
	t->name = name;
	return p;
}


/*!
 * \brief Helper function to parse a URI transformation
 * \param in parsed string
 * \param t transformation
 * \return pointer to the end of the transformation in the string - '}', null on error
 */
char *tr_parse_uri(str *in, trans_t *t)
{
	char *p;
	char *p0;
	char *ps;
	str name;
	str s;
	pv_spec_t *spec = NULL;
	tr_param_t *tp = NULL;

	if(in == NULL || in->s == NULL || t == NULL)
		return NULL;

	p = in->s;
	name.s = in->s;
	t->type = TR_URI;
	t->trf = tr_eval_uri;

	/* find next token */
	while(*p && *p != TR_PARAM_MARKER && *p != TR_RBRACKET)
		p++;
	if(*p == '\0') {
		LM_ERR("invalid transformation: %.*s\n", in->len, in->s);
		goto error;
	}
	name.len = p - name.s;
	trim(&name);

	if(name.len == 4 && strncasecmp(name.s, "user", 4) == 0) {
		t->subtype = TR_URI_USER;
		goto done;
	} else if((name.len == 4 && strncasecmp(name.s, "host", 4) == 0)
			  || (name.len == 6 && strncasecmp(name.s, "domain", 6) == 0)) {
		t->subtype = TR_URI_HOST;
		goto done;
	} else if(name.len == 6 && strncasecmp(name.s, "passwd", 6) == 0) {
		t->subtype = TR_URI_PASSWD;
		goto done;
	} else if(name.len == 4 && strncasecmp(name.s, "port", 4) == 0) {
		t->subtype = TR_URI_PORT;
		goto done;
	} else if(name.len == 4 && strncasecmp(name.s, "saor", 4) == 0) {
		t->subtype = TR_URI_SAOR;
		goto done;
	} else if(name.len == 4 && strncasecmp(name.s, "suri", 4) == 0) {
		t->subtype = TR_URI_SURI;
		goto done;
	} else if(name.len == 4 && strncasecmp(name.s, "duri", 4) == 0) {
		t->subtype = TR_URI_DURI;
		goto done;
	} else if(name.len == 6 && strncasecmp(name.s, "params", 6) == 0) {
		t->subtype = TR_URI_PARAMS;
		goto done;
	} else if(name.len == 7 && strncasecmp(name.s, "headers", 7) == 0) {
		t->subtype = TR_URI_HEADERS;
		goto done;
	} else if(name.len == 5 && strncasecmp(name.s, "param", 5) == 0) {
		t->subtype = TR_URI_PARAM;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid param transformation: %.*s\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_sparam(p, p0, tp, spec, ps, in, s);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid param transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		goto done;
	} else if(name.len == 7 && strncasecmp(name.s, "rmparam", 7) == 0) {
		t->subtype = TR_URI_RMPARAM;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid param transformation: %.*s\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_sparam(p, p0, tp, spec, ps, in, s);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid param transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		goto done;
	} else if(name.len == 9 && strncasecmp(name.s, "transport", 9) == 0) {
		t->subtype = TR_URI_TRANSPORT;
		goto done;
	} else if(name.len == 3 && strncasecmp(name.s, "ttl", 3) == 0) {
		t->subtype = TR_URI_TTL;
		goto done;
	} else if(name.len == 6 && strncasecmp(name.s, "uparam", 6) == 0) {
		t->subtype = TR_URI_UPARAM;
		goto done;
	} else if(name.len == 5 && strncasecmp(name.s, "maddr", 5) == 0) {
		t->subtype = TR_URI_MADDR;
		goto done;
	} else if(name.len == 6 && strncasecmp(name.s, "method", 6) == 0) {
		t->subtype = TR_URI_METHOD;
		goto done;
	} else if(name.len == 2 && strncasecmp(name.s, "lr", 2) == 0) {
		t->subtype = TR_URI_LR;
		goto done;
	} else if(name.len == 2 && strncasecmp(name.s, "r2", 2) == 0) {
		t->subtype = TR_URI_R2;
		goto done;
	} else if(name.len == 6 && strncasecmp(name.s, "scheme", 6) == 0) {
		t->subtype = TR_URI_SCHEME;
		goto done;
	} else if(name.len == 8 && strncasecmp(name.s, "tosocket", 8) == 0) {
		t->subtype = TR_URI_TOSOCKET;
		goto done;
	}

	LM_ERR("unknown transformation: %.*s/%.*s!\n", in->len, in->s, name.len,
			name.s);
error:
	if(spec)
		pv_spec_free(spec);
	return NULL;

done:
	t->name = name;
	return p;
}


/*!
 * \brief Helper function to parse a parameter transformation
 * \param in parsed string
 * \param t transformation
 * \return pointer to the end of the transformation in the string - '}', null on error
 */
char *tr_parse_paramlist(str *in, trans_t *t)
{
	char *p;
	char *p0;
	char *ps;
	char *start_pos;
	str s;
	str name;
	long n;
	int sign;
	pv_spec_t *spec = NULL;
	tr_param_t *tp = NULL;

	if(in == NULL || in->s == NULL || t == NULL)
		return NULL;

	p = in->s;
	name.s = in->s;
	t->type = TR_PARAMLIST;
	t->trf = tr_eval_paramlist;

	/* find next token */
	while(is_in_str(p, in) && *p != TR_PARAM_MARKER && *p != TR_RBRACKET)
		p++;
	if(*p == '\0') {
		LM_ERR("invalid transformation: %.*s\n", in->len, in->s);
		goto error;
	}
	name.len = p - name.s;
	trim(&name);

	if(name.len == 5 && strncasecmp(name.s, "value", 5) == 0) {
		t->subtype = TR_PL_VALUE;
	} else if(name.len == 2 && strncasecmp(name.s, "in", 2) == 0) {
		t->subtype = TR_PL_IN;
	} else if(name.len == 7 && strncasecmp(name.s, "valueat", 7) == 0) {
		t->subtype = TR_PL_VALUEAT;
	} else if(name.len == 4 && strncasecmp(name.s, "name", 4) == 0) {
		t->subtype = TR_PL_NAME;
	} else if(name.len == 5 && strncasecmp(name.s, "count", 5) == 0) {
		t->subtype = TR_PL_COUNT;
	} else {
		goto unknown;
	}

	if(t->subtype == TR_PL_COUNT) {
		if(*p == TR_PARAM_MARKER) {
			start_pos = ++p;
			_tr_parse_sparamx(p, p0, tp, spec, ps, in, s, 1);
			t->params = tp;
			tp = 0;
			if(t->params->type != TR_PARAM_SPEC && p - start_pos != 1) {
				LM_ERR("invalid separator in transformation: "
					   "%.*s\n",
						in->len, in->s);
				goto error;
			}
			while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
				p++;
			if(*p != TR_RBRACKET) {
				LM_ERR("invalid name transformation: %.*s!\n", in->len, in->s);
				goto error;
			}
		}
		goto done;
	}

	/* now transformations with mandatory parameters */
	if(*p != TR_PARAM_MARKER) {
		LM_ERR("invalid %.*s transformation: %.*s\n", name.len, name.s, in->len,
				in->s);
		goto error;
	}
	p++;

	if(t->subtype == TR_PL_VALUE || t->subtype == TR_PL_IN) {
		_tr_parse_sparam(p, p0, tp, spec, ps, in, s);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
	} else if(t->subtype == TR_PL_VALUEAT || t->subtype == TR_PL_NAME) {
		_tr_parse_nparam(p, p0, tp, spec, n, sign, in, s) t->params = tp;
		tp = 0;
		while(is_in_str(p, in) && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
	}

	if(*p == TR_PARAM_MARKER) {
		start_pos = ++p;
		_tr_parse_sparam(p, p0, tp, spec, ps, in, s);
		t->params->next = tp;
		tp = 0;
		if(p - start_pos != 1) {
			LM_ERR("invalid separator in transformation: "
				   "%.*s\n",
					in->len, in->s);
			goto error;
		}
	}

	while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
		p++;
	if(*p != TR_RBRACKET) {
		LM_ERR("invalid %.*s transformation: %.*s!\n", name.len, name.s,
				in->len, in->s);
		goto error;
	}

done:
	t->name = name;
	return p;

unknown:
	LM_ERR("unknown transformation: %.*s/%.*s!\n", in->len, in->s, name.len,
			name.s);
error:
	if(spec)
		pv_spec_free(spec);
	return NULL;
}


/*!
 * \brief Helper function to parse a name-address transformation
 * \param in parsed string
 * \param t transformation
 * \return pointer to the end of the transformation in the string - '}', null on error
 */
char *tr_parse_nameaddr(str *in, trans_t *t)
{
	char *p;
	str name;

	if(in == NULL || t == NULL)
		return NULL;

	p = in->s;
	name.s = in->s;
	t->type = TR_NAMEADDR;
	t->trf = tr_eval_nameaddr;

	/* find next token */
	while(is_in_str(p, in) && *p != TR_PARAM_MARKER && *p != TR_RBRACKET)
		p++;
	if(*p == '\0') {
		LM_ERR("invalid transformation: %.*s\n", in->len, in->s);
		goto error;
	}
	name.len = p - name.s;
	trim(&name);

	if(name.len == 3 && strncasecmp(name.s, "uri", 3) == 0) {
		t->subtype = TR_NA_URI;
		goto done;
	} else if(name.len == 3 && strncasecmp(name.s, "len", 3) == 0) {
		t->subtype = TR_NA_LEN;
		goto done;
	} else if(name.len == 4 && strncasecmp(name.s, "name", 4) == 0) {
		t->subtype = TR_NA_NAME;
		goto done;
	}


	LM_ERR("unknown transformation: %.*s/%.*s/%d!\n", in->len, in->s, name.len,
			name.s, name.len);
error:
	return NULL;
done:
	t->name = name;
	return p;
}

/*!
 * \brief Helper function to parse a name-address transformation
 * \param in parsed string
 * \param t transformation
 * \return pointer to the end of the transformation in the string - '}', null on error
 */
char *tr_parse_tobody(str *in, trans_t *t)
{
	char *p;
	str name;

	if(in == NULL || t == NULL)
		return NULL;

	p = in->s;
	name.s = in->s;
	t->type = TR_TOBODY;
	t->trf = tr_eval_tobody;

	/* find next token */
	while(is_in_str(p, in) && *p != TR_PARAM_MARKER && *p != TR_RBRACKET)
		p++;
	if(*p == '\0') {
		LM_ERR("invalid transformation: %.*s\n", in->len, in->s);
		goto error;
	}
	name.len = p - name.s;
	trim(&name);

	if(name.len == 3 && strncasecmp(name.s, "uri", 3) == 0) {
		t->subtype = TR_TOBODY_URI;
		goto done;
	} else if(name.len == 3 && strncasecmp(name.s, "tag", 3) == 0) {
		t->subtype = TR_TOBODY_TAG;
		goto done;
	} else if(name.len == 4 && strncasecmp(name.s, "user", 4) == 0) {
		t->subtype = TR_TOBODY_URI_USER;
		goto done;
	} else if(name.len == 4 && strncasecmp(name.s, "host", 4) == 0) {
		t->subtype = TR_TOBODY_URI_HOST;
		goto done;
	} else if(name.len == 6 && strncasecmp(name.s, "params", 6) == 0) {
		t->subtype = TR_TOBODY_PARAMS;
		goto done;
	} else if(name.len == 7 && strncasecmp(name.s, "display", 7) == 0) {
		t->subtype = TR_TOBODY_DISPLAY;
		goto done;
	}


	LM_ERR("unknown transformation: %.*s/%.*s/%d!\n", in->len, in->s, name.len,
			name.s, name.len);
error:
	return NULL;
done:
	t->name = name;
	return p;
}

/*!
 * \brief Helper function to parse a line transformation
 * \param in parsed string
 * \param t transformation
 * \return pointer to the end of the transformation in the string - '}', null on error
 */
char *tr_parse_line(str *in, trans_t *t)
{
	char *p;
	char *p0;
	char *ps;
	str s;
	str name;
	long n;
	int sign;
	pv_spec_t *spec = NULL;
	tr_param_t *tp = NULL;


	if(in == NULL || t == NULL)
		return NULL;

	p = in->s;
	name.s = in->s;
	t->type = TR_LINE;
	t->trf = tr_eval_line;

	/* find next token */
	while(is_in_str(p, in) && *p != TR_PARAM_MARKER && *p != TR_RBRACKET)
		p++;
	if(*p == '\0') {
		LM_ERR("invalid transformation: %.*s\n", in->len, in->s);
		goto error;
	}
	name.len = p - name.s;
	trim(&name);

	if(name.len == 2 && strncasecmp(name.s, "at", 2) == 0) {
		t->subtype = TR_LINE_AT;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid name transformation: %.*s\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_nparam(p, p0, tp, spec, n, sign, in, s) t->params = tp;
		tp = 0;
		while(is_in_str(p, in) && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid name transformation: %.*s!\n", in->len, in->s);
			goto error;
		}

		goto done;
	} else if(name.len == 2 && strncasecmp(name.s, "sw", 2) == 0) {
		t->subtype = TR_LINE_SW;
		if(*p != TR_PARAM_MARKER) {
			LM_ERR("invalid value transformation: %.*s\n", in->len, in->s);
			goto error;
		}
		p++;
		_tr_parse_sparam(p, p0, tp, spec, ps, in, s);
		t->params = tp;
		tp = 0;
		while(*p && (*p == ' ' || *p == '\t' || *p == '\n'))
			p++;
		if(*p != TR_RBRACKET) {
			LM_ERR("invalid value transformation: %.*s!\n", in->len, in->s);
			goto error;
		}
		goto done;
	} else if(name.len == 5 && strncasecmp(name.s, "count", 5) == 0) {
		t->subtype = TR_LINE_COUNT;
		goto done;
	}


	LM_ERR("unknown transformation: %.*s/%.*s/%d!\n", in->len, in->s, name.len,
			name.s, name.len);
error:
	return NULL;
done:
	t->name = name;
	return p;
}

/*!
 * \brief Helper function to parse urialias transformation
 * \param in parsed string
 * \param t transformation
 * \return pointer to the end of the transformation in the string - '}', null on error
 */
char *tr_parse_urialias(str *in, trans_t *t)
{
	char *p;
	str name;


	if(in == NULL || t == NULL)
		return NULL;

	p = in->s;
	name.s = in->s;
	t->type = TR_URIALIAS;
	t->trf = tr_eval_urialias;

	/* find next token */
	while(is_in_str(p, in) && *p != TR_PARAM_MARKER && *p != TR_RBRACKET)
		p++;
	if(*p == '\0') {
		LM_ERR("invalid transformation: %.*s\n", in->len, in->s);
		goto error;
	}
	name.len = p - name.s;
	trim(&name);

	if(name.len == 6 && strncasecmp(name.s, "encode", 6) == 0) {
		t->subtype = TR_URIALIAS_ENCODE;
		goto done;
	} else if(name.len == 6 && strncasecmp(name.s, "decode", 6) == 0) {
		t->subtype = TR_URIALIAS_DECODE;
		goto done;
	}


	LM_ERR("unknown transformation: %.*s/%.*s/%d!\n", in->len, in->s, name.len,
			name.s, name.len);
error:
	return NULL;

done:
	t->name = name;
	return p;
}


/*!
 * \brief Helper function to parse val transformation
 * \param in parsed string
 * \param t transformation
 * \return pointer to the end of the transformation in the string - '}', null on error
 */
char *tr_parse_val(str *in, trans_t *t)
{
	char *p;
	str name;


	if(in == NULL || t == NULL)
		return NULL;

	p = in->s;
	name.s = in->s;
	t->type = TR_VAL;
	t->trf = tr_eval_val;

	/* find next token */
	while(is_in_str(p, in) && *p != TR_PARAM_MARKER && *p != TR_RBRACKET)
		p++;
	if(*p == '\0') {
		LM_ERR("invalid transformation: %.*s\n", in->len, in->s);
		goto error;
	}
	name.len = p - name.s;
	trim(&name);

	if(name.len == 2 && strncasecmp(name.s, "n0", 2) == 0) {
		t->subtype = TR_VAL_N0;
		goto done;
	} else if(name.len == 2 && strncasecmp(name.s, "ne", 2) == 0) {
		t->subtype = TR_VAL_NE;
		goto done;
	} else if(name.len == 4 && strncasecmp(name.s, "json", 4) == 0) {
		t->subtype = TR_VAL_JSON;
		goto done;
	} else if(name.len == 6 && strncasecmp(name.s, "jsonqe", 6) == 0) {
		t->subtype = TR_VAL_JSONQE;
		goto done;
	}


	LM_ERR("unknown transformation: %.*s/%.*s/%d!\n", in->len, in->s, name.len,
			name.s, name.len);
error:
	return NULL;

done:
	t->name = name;
	return p;
}

/*!
 * \brief Helper function to parse num transformation
 * \param in parsed string
 * \param t transformation
 * \return pointer to the end of the transformation in the string - '}', null on error
 */
char *tr_parse_num(str *in, trans_t *t)
{
	char *p;
	str name;

	if(in == NULL || t == NULL)
		return NULL;

	p = in->s;
	name.s = in->s;
	t->type = TR_NUM;
	t->trf = tr_eval_num;

	/* find next token */
	while(is_in_str(p, in) && *p != TR_PARAM_MARKER && *p != TR_RBRACKET)
		p++;
	if(*p == '\0') {
		LM_ERR("invalid transformation: %.*s\n", in->len, in->s);
		goto error;
	}
	name.len = p - name.s;
	trim(&name);

	if(name.len == 6 && strncasecmp(name.s, "fdigit", 6) == 0) {
		t->subtype = TR_NUM_FDIGIT;
		goto done;
	} else if(name.len == 6 && strncasecmp(name.s, "ldigit", 6) == 0) {
		t->subtype = TR_NUM_LDIGIT;
		goto done;
	}


	LM_ERR("unknown transformation: %.*s/%.*s/%d!\n", in->len, in->s, name.len,
			name.s, name.len);
error:
	return NULL;

done:
	t->name = name;
	return p;
}
