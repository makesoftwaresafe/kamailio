/*
 * Copyright (C) 2018 Andreas Granig (sipwise.com)
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


#ifndef _REDIS_DBASE_H_
#define _REDIS_DBASE_H_

#include "db_redis_mod.h"

#define SREM_KEY_LUA                                                         \
	"redis.call('SREM', KEYS[1], KEYS[3]); if redis.call('SCARD', KEYS[1]) " \
	"== 0 then redis.call('SREM', KEYS[2], KEYS[1]) end"
#define HDEL_KEY_LUA                                                        \
	"redis.call('HDEL', KEYS[1], KEYS[3]); if redis.call('HLEN', KEYS[1]) " \
	"== 0 then redis.call('HDEL', KEYS[2], KEYS[1]) end"


/*
 * Initialize database connection
 */
db1_con_t *db_redis_init(const str *_sqlurl);

/*
 * Close a database connection
 */
void db_redis_close(db1_con_t *_h);

/*
 * Free all memory allocated by get_result
 */
int db_redis_free_result(db1_con_t *_h, db1_res_t *_r);

/*
 * Do a query
 */
int db_redis_query(const db1_con_t *_h, const db_key_t *_k, const db_op_t *_op,
		const db_val_t *_v, const db_key_t *_c, const int _n, const int _nc,
		const db_key_t _o, db1_res_t **_r);

/*
 * Fetch rows from a result
 */
int db_redis_fetch_result(const db1_con_t *_h, db1_res_t **_r, const int nrows);

/*
 * Raw SQL query
 */
int db_redis_raw_query(const db1_con_t *_h, const str *_s, db1_res_t **_r);

/*
 * Insert a row into table
 */
int db_redis_insert(const db1_con_t *_h, const db_key_t *_k, const db_val_t *_v,
		const int _n);

/*
 * Delete a row from table
 */
int db_redis_delete(const db1_con_t *_h, const db_key_t *_k, const db_op_t *_o,
		const db_val_t *_v, const int _n);

/*
 * Update a row in table
 */
int db_redis_update(const db1_con_t *_h, const db_key_t *_k, const db_op_t *_o,
		const db_val_t *_v, const db_key_t *_uk, const db_val_t *_uv,
		const int _n, const int _un);

/*
 * Just like insert, but replace the row if it exists
 */
int db_redis_replace(const db1_con_t *handle, const db_key_t *keys,
		const db_val_t *vals, const int n, const int _un, const int _m);

/*
 * Store name of table that will be used by
 * subsequent database functions
 */
int db_redis_use_table(db1_con_t *_h, const str *_t);

#endif /* _REDIS_DBASE_H_ */
