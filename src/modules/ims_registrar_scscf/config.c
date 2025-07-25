/*
 * $Id$
 * This file is part of SIP-router, a free SIP server.
 *
 * SIP-router is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * SIP-router is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/*!
 * \file
 * \brief Registrar :: Configuration
 * \ingroup Registrar
 */


#include "../../core/cfg/cfg.h"
#include "../../core/parser/msg_parser.h" /* method types */

#include "config.h"

struct cfg_group_registrar default_registrar_cfg = {
		.default_expires = 3600,	/* default_expires */
		.default_expires_range = 0, /* default_expires_range */
		.min_expires = 60,			/* min_expires */
		.max_expires = 600000,		/* max_expires */
		.em_default_expires = 1800, /* emergency contact default expires */
		.em_max_expires = 3600,		/* emergency contact max expires */
		.em_min_expires = 0,		/* emergency contact min expires */
		.max_contacts = 0,			/* max_contacts */
		.retry_after = 0,			/* retry_after */
		.case_sensitive = 0,		/* case_sensitive */
		.default_q = Q_UNSPECIFIED, /* default_q */
		.append_branches = 1,		/* append_branches */
		.realm_pref = ""			/* realm_pref */
};

void *registrar_cfg = &default_registrar_cfg;

cfg_def_t registrar_cfg_def[] = {
		{"default_expires", CFG_VAR_INT | CFG_CB_ONLY_ONCE, 0, 0, 0,
				default_expires_stats_update,
				"Contains number of second to expire if no expire hf or "
				"contact expire present"},
		{"default_expires_range", CFG_VAR_INT | CFG_CB_ONLY_ONCE, 0, 100, 0,
				default_expires_range_update,
				"Percent from default_expires that will be used in "
				"generating the range for the expire interval"},
		{"min_expires", CFG_VAR_INT | CFG_CB_ONLY_ONCE, 0, 0, 0, 0,
				"The minimum expires value of a Contact. Value 0 "
				"disables the checking. "},
		{"max_expires", CFG_VAR_INT | CFG_CB_ONLY_ONCE, 0, 0, 0,
				max_expires_stats_update,
				"The maximum expires value of a Contact. Value 0 "
				"disables the checking. "},
		{"em_default_expires", CFG_VAR_INT | CFG_CB_ONLY_ONCE, 0, 0, 0, 0,
				"The default emergency expires value of a Contact."},
		{"em_max_expires", CFG_VAR_INT | CFG_CB_ONLY_ONCE, 0, 0, 0, 0,
				"The maximum emergency expires value of a Contact. "
				"Value 0 disables the checking. "},
		{"em_min_expires", CFG_VAR_INT | CFG_CB_ONLY_ONCE, 0, 0, 0, 0,
				"The maximum emergency expires value of a Contact. "
				"Value 0 disables the checking. "},
		{"max_contacts", CFG_VAR_INT | CFG_ATOMIC, 0, 0, 0, 0,
				"The maximum number of Contacts for an AOR. Value 0 "
				"disables the checking. "},
		{"retry_after", CFG_VAR_INT | CFG_ATOMIC, 0, 0, 0, 0,
				"If you want to add the Retry-After header field in "
				"5xx replies, set this parameter to a value grater "
				"than zero"},
		{"case_sensitive", CFG_VAR_INT | CFG_ATOMIC, 0, 0, 0, 0,
				"If set to 1 then AOR comparison will be case "
				"sensitive. Recommended and default is 0, case "
				"insensitive"},
		{"default_q", CFG_VAR_INT | CFG_ATOMIC, -1, 1000, 0, 0,
				"The parameter represents default q value for new "
				"contacts."}, /* Q_UNSPECIFIED is -1 */
		{"append_branches", CFG_VAR_INT, 0, 0, 0, 0,
				"If set to 1(default), lookup will put all contacts "
				"found in msg structure"},
		{"realm_pref", CFG_VAR_STRING, 0, 0, 0, 0,
				"Realm prefix to be removed. Default is \"\""},
		{0, 0, 0, 0, 0, 0}};
