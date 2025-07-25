/*
 * PERMISSIONS module
 *
 * Copyright (C) 2003 Miklós Tirpák (mtirpak@sztaki.hu)
 * Copyright (C) 2003 iptel.org
 * Copyright (C) 2003-2007 Juha Heinanen
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

#include <stdio.h>
#include "permissions.h"
#include "parse_config.h"
#include "trusted.h"
#include "address.h"
#include "hash.h"
#include "rpc.h"
#include "../../core/mem/mem.h"
#include "../../core/parser/parse_from.h"
#include "../../core/parser/parse_uri.h"
#include "../../core/parser/parse_refer_to.h"
#include "../../core/parser/contact/parse_contact.h"
#include "../../core/str.h"
#include "../../core/dset.h"
#include "../../core/globals.h"
#include "../../core/mod_fix.h"
#include "../../core/ut.h"
#include "../../core/rpc.h"
#include "../../core/rpc_lookup.h"
#include "../../core/kemi.h"

MODULE_VERSION

static rule_file_t perm_allow[MAX_RULE_FILES]; /* Parsed allow files */
static rule_file_t perm_deny[MAX_RULE_FILES];  /* Parsed deny files */
static int perm_rules_num = 0; /* Number of parsed allow/deny files */


/* Module parameter variables */
static char *perm_default_allow_file = DEFAULT_ALLOW_FILE;
static char *perm_default_deny_file = DEFAULT_DENY_FILE;
char *perm_allow_suffix = ".allow";
static char *perm_deny_suffix = ".deny";


/* for allow_trusted and allow_address function */
str perm_db_url = {NULL, 0}; /* Don't connect to the database by default */
int perm_reload_delta = 5;
int perm_trusted_table_interval = 60;

/* for allow_trusted function */
int perm_db_mode = DISABLE_CACHE; /* Database usage mode: 0=no cache, 1=cache */
str perm_trusted_table = str_init("trusted"); /* Name of trusted table */
str perm_source_col = str_init("src_ip"); /* Name of source address column */
str perm_proto_col = str_init("proto");	  /* Name of protocol column */
str perm_from_col = str_init("from_pattern"); /* Name of from pattern column */
str perm_ruri_col = str_init("ruri_pattern"); /* Name of RURI pattern column */
str perm_tag_col = str_init("tag");			  /* Name of tag column */
str perm_priority_col = str_init("priority"); /* Name of priority column */
str perm_tag_avp_param = {NULL, 0};			  /* Peer tag AVP spec */
int perm_peer_tag_mode = 0; /* Add tags form all mathcing peers to avp */

/* for allow_address function */
str perm_address_table = str_init("address"); /* Name of address table */
str perm_grp_col = str_init("grp");			  /* Name of address group column */
str perm_ip_addr_col = str_init("ip_addr");	  /* Name of ip address column */
str perm_mask_col = str_init("mask");		  /* Name of mask column */
str perm_port_col = str_init("port");		  /* Name of port column */

static str perm_address_file_param =
		STR_NULL;				  /* Path to file with address records */
str perm_address_file = STR_NULL; /* Full path to file with address records */

/*
 * By default we check all branches
 */
static int perm_check_all_branches = 1;

time_t *perm_rpc_reload_time = NULL;
int _perm_max_subnets = 512;

int _perm_load_backends = 0xFFFF;
int _perm_subnet_match_mode = 0;

/*
 * Convert the name of the files into table index
 */
static int load_fixup(void **param, int param_no);

/*
 * Convert the name of the file into table index, this
 * function takes just one name, appends .allow and .deny
 * to and and the rest is same as in load_fixup
 */
static int single_fixup(void **param, int param_no);


/*
 * Parse pseudo variable parameter
 */
static int double_fixup(void **param, int param_no);

static int allow_routing_0(struct sip_msg *msg, char *str1, char *str2);
static int allow_routing_1(struct sip_msg *msg, char *basename, char *str2);
static int allow_routing_2(
		struct sip_msg *msg, char *allow_file, char *deny_file);
static int allow_register_1(struct sip_msg *msg, char *basename, char *s);
static int allow_register_2(
		struct sip_msg *msg, char *allow_file, char *deny_file);
static int allow_register_include_port_1(
		struct sip_msg *msg, char *basename, char *s);
static int allow_register_include_port_2(
		struct sip_msg *msg, char *allow_file, char *deny_file);
static int allow_uri(struct sip_msg *msg, char *basename, char *uri);

static int mod_init(void);
static void mod_exit(void);
static int child_init(int rank);
static int permissions_init_rpc(void);


/* clang-format off */
/* Exported functions */
static cmd_export_t cmds[] = {
	{"allow_routing", (cmd_function)allow_routing_0, 0, 0, 0, ANY_ROUTE},
	{"allow_routing", (cmd_function)allow_routing_1, 1, single_fixup, 0,
			ANY_ROUTE},
	{"allow_routing", (cmd_function)allow_routing_2, 2, load_fixup, 0,
			ANY_ROUTE},
	{"allow_register", (cmd_function)allow_register_1, 1, single_fixup, 0,
			REQUEST_ROUTE | FAILURE_ROUTE},
	{"allow_register", (cmd_function)allow_register_2, 2, load_fixup, 0,
			REQUEST_ROUTE | FAILURE_ROUTE},
	{"allow_register_include_port",
			(cmd_function)allow_register_include_port_1, 1, single_fixup, 0,
			REQUEST_ROUTE | FAILURE_ROUTE},
	{"allow_register_include_port",
			(cmd_function)allow_register_include_port_2, 2, load_fixup, 0,
			REQUEST_ROUTE | FAILURE_ROUTE},
	{"allow_trusted", (cmd_function)allow_trusted_0, 0, 0, 0, ANY_ROUTE},
	{"allow_trusted", (cmd_function)allow_trusted_2, 2, fixup_spve_spve,
			fixup_free_spve_spve, ANY_ROUTE},
	{"allow_trusted", (cmd_function)allow_trusted_3, 3, fixup_spve_all,
			fixup_free_spve_all, ANY_ROUTE},
	{"allow_uri", (cmd_function)allow_uri, 2, double_fixup, 0,
			REQUEST_ROUTE | FAILURE_ROUTE},
	{"allow_address", (cmd_function)w_allow_address, 3, fixup_isi,
			fixup_free_isi, ANY_ROUTE},
	{"allow_source_address", (cmd_function)w_allow_source_address, 1,
			fixup_igp_null, fixup_free_igp_null, ANY_ROUTE},
	{"allow_source_address", (cmd_function)w_allow_source_address, 0, 0, 0,
			ANY_ROUTE},
	{"allow_source_address_group", (cmd_function)allow_source_address_group,
			0, 0, 0, ANY_ROUTE},
	{"allow_address_group", (cmd_function)allow_address_group, 2,
			fixup_spve_igp, fixup_free_spve_igp, ANY_ROUTE},
	{0, 0, 0, 0, 0, 0}
};

/* Exported parameters */
static param_export_t params[] = {
	{"default_allow_file", PARAM_STRING, &perm_default_allow_file},
	{"default_deny_file", PARAM_STRING, &perm_default_deny_file},
	{"check_all_branches", PARAM_INT, &perm_check_all_branches},
	{"allow_suffix", PARAM_STRING, &perm_allow_suffix},
	{"deny_suffix", PARAM_STRING, &perm_deny_suffix},
	{"db_url", PARAM_STR, &perm_db_url},
	{"db_mode", PARAM_INT, &perm_db_mode},
	{"trusted_table", PARAM_STR, &perm_trusted_table},
	{"source_col", PARAM_STR, &perm_source_col},
	{"proto_col", PARAM_STR, &perm_proto_col},
	{"from_col", PARAM_STR, &perm_from_col},
	{"ruri_col", PARAM_STR, &perm_ruri_col},
	{"tag_col", PARAM_STR, &perm_tag_col},
	{"priority_col", PARAM_STR, &perm_priority_col},
	{"peer_tag_avp", PARAM_STR, &perm_tag_avp_param},
	{"peer_tag_mode", PARAM_INT, &perm_peer_tag_mode},
	{"address_table", PARAM_STR, &perm_address_table},
	{"address_file", PARAM_STR, &perm_address_file_param},
	{"grp_col", PARAM_STR, &perm_grp_col},
	{"ip_addr_col", PARAM_STR, &perm_ip_addr_col},
	{"mask_col", PARAM_STR, &perm_mask_col},
	{"port_col", PARAM_STR, &perm_port_col},
	{"max_subnets", PARAM_INT, &_perm_max_subnets},
	{"subnet_match_mode", PARAM_INT, &_perm_subnet_match_mode},
	{"load_backends", PARAM_INT, &_perm_load_backends},
	{"reload_delta", PARAM_INT, &perm_reload_delta},
	{"trusted_cleanup_interval", PARAM_INT, &perm_trusted_table_interval},
	{0, 0, 0}
};

/* Module interface */
struct module_exports exports = {
	"permissions",	 /* module name */
	DEFAULT_DLFLAGS, /* dlopen flags */
	cmds,			 /* Exported functions */
	params,			 /* Exported parameters */
	0,				 /* RPC method exports */
	0,				 /* exported pseudo-variables */
	0,				 /* response function */
	mod_init,		 /* module initialization function */
	child_init,		 /* child initialization function */
	mod_exit		 /* destroy function */
};
/* clang-format on */


/*
 * Extract path (the beginning of the string
 * up to the last / character
 * Returns length of the path
 */
static int get_path(char *pathname)
{
	char *c;
	if(!pathname)
		return 0;

	c = strrchr(pathname, '/');
	if(!c)
		return 0;

	return c - pathname + 1;
}


/*
 * Prepend path if necessary
 */
static char *get_pathname(char *name)
{
	char *buffer;
	int path_len, name_len;

	if(!name)
		return 0;

	name_len = strlen(name);
	if(strchr(name, '/')) {
		buffer = (char *)pkg_malloc(name_len + 1);
		if(!buffer)
			goto err;
		strcpy(buffer, name);
		return buffer;
	} else {
		path_len = get_path(cfg_file);
		buffer = (char *)pkg_malloc(path_len + name_len + 1);
		if(!buffer)
			goto err;
		memcpy(buffer, cfg_file, path_len);
		memcpy(buffer + path_len, name, name_len);
		buffer[path_len + name_len] = '\0';
		return buffer;
	}

err:
	LM_ERR("no pkg memory left\n");
	return 0;
}


/*
 * If the file pathname has been parsed already then the
 * function returns its index in the tables, otherwise it
 * returns -1 to indicate that the file needs to be read
 * and parsed yet
 */
static int find_index(rule_file_t *array, char *pathname)
{
	int i;

	for(i = 0; i < perm_rules_num; i++) {
		if(!strcmp(pathname, array[i].filename))
			return i;
	}

	return -1;
}


/*
 * Return URI without all the bells and whistles, that means only
 * sip:username@domain, resulting buffer is statically allocated and
 * zero terminated
 */
static char *get_plain_uri(const str *uri, int check_port)
{
	static char buffer[EXPRESSION_LENGTH + 1];
	struct sip_uri puri;
	int len;

	if(!uri)
		return 0;

	if(parse_uri(uri->s, uri->len, &puri) < 0) {
		LM_ERR("failed to parse URI\n");
		return 0;
	}

	if(puri.user.len) {
		len = puri.user.len + puri.host.len + 5; /* +5 is 'sip:' and '@' */
	} else {
		len = puri.host.len + 4; /* +4 is 'sip:' */
	}

	if(check_port && puri.port.len) {
		LM_DBG("Port number will also be used to check the Contact value\n");
		len += (puri.port.len + 1); /* +1 is ':' */
	}

	if(len > EXPRESSION_LENGTH) {
		LM_ERR("Request-URI is too long: %d chars\n", len);
		return 0;
	}

	strcpy(buffer, "sip:");
	if(puri.user.len) {
		memcpy(buffer + 4, puri.user.s, puri.user.len); /* +4 is 'sip:' */
		buffer[puri.user.len + 4] = '@';
		memcpy(buffer + puri.user.len + 5, puri.host.s,
				puri.host.len); /* +5 is 'sip:' and '@' */
		if(check_port && puri.port.len) {
			buffer[puri.user.len + puri.host.len + 5] = ':';
			memcpy(buffer + puri.user.len + puri.host.len + 6, puri.port.s,
					puri.port.len); /* +6 is 'sip:', '@' and ':' */
		}
	} else {
		memcpy(buffer + 4, puri.host.s, puri.host.len);
		if(check_port && puri.port.len) {
			buffer[puri.host.len + 4] = ':';
			memcpy(buffer + puri.host.len + 5, puri.port.s,
					puri.port.len); /* +5 is 'sip:' and ':' */
		}
	}

	buffer[len] = '\0';
	return buffer;
}


/*
 * determines the permission of the call
 * return values:
 * -1:	deny
 * 1:	allow
 */
static int check_routing(struct sip_msg *msg, int idx)
{
	struct hdr_field *from;
	int len, q;
	static char from_str[EXPRESSION_LENGTH + 1];
	static char ruri_str[EXPRESSION_LENGTH + 1];
	char *uri_str;
	str branch;
	int br_idx;

	/* turn off control, allow any routing */
	if((!perm_allow[idx].rules) && (!perm_deny[idx].rules)) {
		LM_DBG("no rules => allow any routing\n");
		return 1;
	}

	/* looking for FROM HF */
	if((!msg->from) && (parse_headers(msg, HDR_FROM_F, 0) == -1)) {
		LM_ERR("failed to parse message\n");
		return -1;
	}

	if(!msg->from) {
		LM_ERR("FROM header field not found\n");
		return -1;
	}

	/* we must call parse_from_header explicitly */
	if((!(msg->from)->parsed) && (parse_from_header(msg) < 0)) {
		LM_ERR("failed to parse From body\n");
		return -1;
	}

	from = msg->from;
	len = ((struct to_body *)from->parsed)->uri.len;
	if(len > EXPRESSION_LENGTH) {
		LM_ERR("From header field is too long: %d chars\n", len);
		return -1;
	}
	strncpy(from_str, ((struct to_body *)from->parsed)->uri.s, len);
	from_str[len] = '\0';

	/* looking for request URI */
	if(parse_sip_msg_uri(msg) < 0) {
		LM_ERR("uri parsing failed\n");
		return -1;
	}

	len = msg->parsed_uri.user.len + msg->parsed_uri.host.len + 5;
	if(len > EXPRESSION_LENGTH) {
		LM_ERR("Request URI is too long: %d chars\n", len);
		return -1;
	}

	strcpy(ruri_str, "sip:");
	memcpy(ruri_str + 4, msg->parsed_uri.user.s, msg->parsed_uri.user.len);
	ruri_str[msg->parsed_uri.user.len + 4] = '@';
	memcpy(ruri_str + msg->parsed_uri.user.len + 5, msg->parsed_uri.host.s,
			msg->parsed_uri.host.len);
	ruri_str[len] = '\0';

	LM_DBG("looking for From: %s Request-URI: %s\n", from_str, ruri_str);
	/* rule exists in allow file */
	if(search_rule(perm_allow[idx].rules, from_str, ruri_str)) {
		if(perm_check_all_branches)
			goto check_branches;
		LM_DBG("allow rule found => routing is allowed\n");
		return 1;
	}

	/* rule exists in deny file */
	if(search_rule(perm_deny[idx].rules, from_str, ruri_str)) {
		LM_DBG("deny rule found => routing is denied\n");
		return -1;
	}

	if(!perm_check_all_branches) {
		LM_DBG("neither allow nor deny rule found => routing is allowed\n");
		return 1;
	}

check_branches:
	for(br_idx = 0; (branch.s = get_branch(
							 br_idx, &branch.len, &q, 0, 0, 0, 0, 0, 0, 0))
					!= 0;
			br_idx++) {
		uri_str = get_plain_uri(&branch, 0);
		if(!uri_str) {
			LM_ERR("failed to extract plain URI\n");
			return -1;
		}
		LM_DBG("looking for From: %s Branch: %s\n", from_str, uri_str);

		if(search_rule(perm_allow[idx].rules, from_str, uri_str)) {
			continue;
		}

		if(search_rule(perm_deny[idx].rules, from_str, uri_str)) {
			LM_DBG("deny rule found for one of branches => routing"
				   "is denied\n");
			return -1;
		}
	}

	LM_DBG("check of branches passed => routing is allowed\n");
	return 1;
}


/*
 * Convert the name of the files into table index
 */
static int load_fixup(void **param, int param_no)
{
	char *pathname;
	int idx;
	rule_file_t *table;

	if(param_no == 1) {
		table = perm_allow;
	} else {
		table = perm_deny;
	}

	pathname = get_pathname(*param);
	idx = find_index(table, pathname);

	if(idx == -1) {
		/* Not opened yet, open the file and parse it */
		table[perm_rules_num].filename = pathname;
		table[perm_rules_num].rules = parse_config_file(pathname);
		if(table[perm_rules_num].rules) {
			LM_DBG("file (%s) parsed\n", pathname);
		} else {
			LM_INFO("file (%s) not parsed properly => empty rule set\n",
					pathname);
		}
		*param = (void *)(long)perm_rules_num;
		if(param_no == 2)
			perm_rules_num++;
	} else {
		/* File already parsed, re-use it */
		LM_DBG("file (%s) already loaded, re-using\n", pathname);
		pkg_free(pathname);
		*param = (void *)(long)idx;
	}

	return 0;
}


/*
 * Convert the name of the file into table index
 */
static int single_fixup(void **param, int param_no)
{
	char *buffer;
	void *tmp;
	int param_len, ret, suffix_len;

	if(param_no != 1)
		return 0;

	param_len = strlen((char *)*param);
	if(strlen(perm_allow_suffix) > strlen(perm_deny_suffix)) {
		suffix_len = strlen(perm_allow_suffix);
	} else {
		suffix_len = strlen(perm_deny_suffix);
	}

	buffer = pkg_malloc(param_len + suffix_len + 1);
	if(!buffer) {
		LM_ERR("no pkg memory left\n");
		return -1;
	}

	strcpy(buffer, (char *)*param);
	strcat(buffer, perm_allow_suffix);
	tmp = buffer;
	ret = load_fixup(&tmp, 1);

	strcpy(buffer + param_len, perm_deny_suffix);
	tmp = buffer;
	ret |= load_fixup(&tmp, 2);

	*param = tmp;

	pkg_free(buffer);
	return ret;
}


/*
 * Convert the name of the file into table index and pvar into parsed pseudo
 * variable specification
 */
static int double_fixup(void **param, int param_no)
{
	char *buffer;
	void *tmp;
	int param_len, ret, suffix_len;
	pv_spec_t *sp;
	str s;

	if(param_no == 1) { /* basename */
		param_len = strlen((char *)*param);
		if(strlen(perm_allow_suffix) > strlen(perm_deny_suffix)) {
			suffix_len = strlen(perm_allow_suffix);
		} else {
			suffix_len = strlen(perm_deny_suffix);
		}

		buffer = pkg_malloc(param_len + suffix_len + 1);
		if(!buffer) {
			LM_ERR("no pkg memory left\n");
			return -1;
		}

		strcpy(buffer, (char *)*param);
		strcat(buffer, perm_allow_suffix);
		tmp = buffer;
		ret = load_fixup(&tmp, 1);

		strcpy(buffer + param_len, perm_deny_suffix);
		tmp = buffer;
		ret |= load_fixup(&tmp, 2);

		*param = tmp;
		pkg_free(buffer);

		return ret;

	} else if(param_no == 2) { /* pseudo variable */

		sp = (pv_spec_t *)pkg_malloc(sizeof(pv_spec_t));
		if(sp == 0) {
			LM_ERR("no pkg memory left\n");
			return -1;
		}
		s.s = (char *)*param;
		s.len = strlen(s.s);
		if(pv_parse_spec(&s, sp) == 0) {
			LM_ERR("parsing of pseudo variable %s failed!\n", (char *)*param);
			pkg_free(sp);
			return -1;
		}

		if(sp->type == PVT_NULL) {
			LM_ERR("bad pseudo variable\n");
			pkg_free(sp);
			return -1;
		}

		*param = (void *)sp;

		return 0;
	}

	*param = (void *)0;

	return 0;
}


/*
 * module initialization function
 */
static int mod_init(void)
{
	if(_perm_load_backends == 0) {
		LM_ERR("failure - no backend to be loaded\n");
		return -1;
	}

	perm_rpc_reload_time = shm_malloc(sizeof(time_t));
	if(perm_rpc_reload_time == NULL) {
		SHM_MEM_ERROR;
		return -1;
	}
	*perm_rpc_reload_time = 0;

	if(perm_reload_delta < 0)
		perm_reload_delta = 5;

	if(permissions_init_rpc() != 0) {
		LM_ERR("failed to register RPC commands\n");
		return -1;
	}

	if(_perm_load_backends & PERM_LOAD_ALLOWFILE) {
		perm_allow[0].filename = get_pathname(perm_default_allow_file);
		perm_allow[0].rules = parse_config_file(perm_allow[0].filename);
		if(perm_allow[0].rules) {
			LM_DBG("default allow file (%s) parsed\n", perm_allow[0].filename);
		} else {
			LM_INFO("default allow file (%s) not found => empty rule set\n",
					perm_allow[0].filename);
		}
	} else {
		perm_allow[0].filename = NULL;
		perm_allow[0].rules = NULL;
	}

	if(_perm_load_backends & PERM_LOAD_DENYFILE) {
		perm_deny[0].filename = get_pathname(perm_default_deny_file);
		perm_deny[0].rules = parse_config_file(perm_deny[0].filename);
		if(perm_deny[0].rules) {
			LM_DBG("default deny file (%s) parsed\n", perm_deny[0].filename);
		} else {
			LM_INFO("default deny file (%s) not found => empty rule set\n",
					perm_deny[0].filename);
		}
	} else {
		perm_deny[0].filename = NULL;
		perm_deny[0].rules = NULL;
	}

	if(init_tag_avp(&perm_tag_avp_param) < 0) {
		LM_ERR("failed to process peer_tag_avp AVP param\n");
		return -1;
	}

	if(_perm_load_backends & PERM_LOAD_TRUSTEDDB) {
		if(init_trusted() != 0) {
			LM_ERR("failed to initialize the allow_trusted function\n");
			return -1;
		}
	}

	if(_perm_load_backends & PERM_LOAD_ADDRESSDB) {
		if(perm_address_file_param.s != NULL
				&& perm_address_file_param.len > 0) {
			perm_address_file.s = get_pathname(perm_address_file_param.s);
			if(perm_address_file.s == NULL) {
				LM_ERR("failed to set full path to address file\n");
				return -1;
			}
			perm_address_file.len = strlen(perm_address_file.s);
		}
		if(init_addresses() != 0) {
			LM_ERR("failed to initialize the allow_address function\n");
			return -1;
		}
	}

	if((perm_db_mode != DISABLE_CACHE) && (perm_db_mode != ENABLE_CACHE)) {
		LM_ERR("invalid db_mode value: %d\n", perm_db_mode);
		return -1;
	}

	perm_rules_num = 1;
	return 0;
}


static int child_init(int rank)
{
	if(_perm_load_backends & PERM_LOAD_TRUSTEDDB) {
		if(init_child_trusted(rank) == -1)
			return -1;
	}
	return 0;
}


/*
 * destroy function
 */
static void mod_exit(void)
{
	int i;

	if(perm_rpc_reload_time != NULL) {
		shm_free(perm_rpc_reload_time);
		perm_rpc_reload_time = 0;
	}

	for(i = 0; i < perm_rules_num; i++) {
		if(perm_allow[i].rules)
			free_rule(perm_allow[i].rules);
		if(perm_allow[i].filename)
			pkg_free(perm_allow[i].filename);

		if(perm_deny[i].rules)
			free_rule(perm_deny[i].rules);
		if(perm_deny[i].filename)
			pkg_free(perm_deny[i].filename);
	}

#if 0
	clean_trusted();

	clean_addresses();
#endif
}


/*
 * Uses default rule files from the module parameters
 */
int allow_routing_0(struct sip_msg *msg, char *str1, char *str2)
{
	return check_routing(msg, 0);
}


int allow_routing_1(struct sip_msg *msg, char *basename, char *s)
{
	return check_routing(msg, (int)(long)basename);
}


/*
 * Accepts allow and deny files as parameters
 */
int allow_routing_2(struct sip_msg *msg, char *allow_file, char *deny_file)
{
	/* Index converted by load_lookup */
	return check_routing(msg, (int)(long)allow_file);
}


/*
 * Test of REGISTER messages. Creates To-Contact pairs and compares them
 * against rules in allow and deny files passed as parameters. The function
 * iterates over all Contacts and creates a pair with To for each contact
 * found. That allows to restrict what IPs may be used in registrations, for
 * example.
 *
 * If check_port is 0, then port isn't taken into account.
 */
static int check_register(struct sip_msg *msg, int idx, int check_port)
{
	int len;
	static char to_str[EXPRESSION_LENGTH + 1];
	char *contact_str;
	contact_t *c;

	/* turn off control, allow any routing */
	if((!perm_allow[idx].rules) && (!perm_deny[idx].rules)) {
		LM_DBG("no rules => allow any registration\n");
		return 1;
	}

	/*
	 * Note: We do not parse the whole header field here although the message can
	 * contain multiple Contact header fields. We try contacts one by one and if one
	 * of them causes reject then we don't look at others, this could improve performance
	 * a little bit in some situations
	 */
	if(parse_headers(msg, HDR_TO_F | HDR_CONTACT_F, 0) == -1) {
		LM_ERR("failed to parse headers\n");
		return -1;
	}

	if(!msg->to) {
		LM_ERR("To or Contact not found\n");
		return -1;
	}

	if(!msg->contact) {
		/* REGISTER messages that contain no Contact header field
		 * are allowed. Such messages do not modify the contents of
		 * the user location database anyway and thus are not harmful
		 */
		LM_DBG("no Contact found, allowing\n");
		return 1;
	}

	/* Check if the REGISTER message contains start Contact and if
	 * so then allow it
	 */
	if(parse_contact(msg->contact) < 0) {
		LM_ERR("failed to parse Contact HF\n");
		return -1;
	}

	if(((contact_body_t *)msg->contact->parsed)->star) {
		LM_DBG("* Contact found, allowing\n");
		return 1;
	}

	len = ((struct to_body *)msg->to->parsed)->uri.len;
	if(len > EXPRESSION_LENGTH) {
		LM_ERR("To header field is too long: %d chars\n", len);
		return -1;
	}
	strncpy(to_str, ((struct to_body *)msg->to->parsed)->uri.s, len);
	to_str[len] = '\0';

	if(contact_iterator(&c, msg, 0) < 0) {
		return -1;
	}

	while(c) {
		/* if check_port = 1, then port is included into regex check */
		contact_str = get_plain_uri(&c->uri, check_port);
		if(!contact_str) {
			LM_ERR("can't extract plain Contact URI\n");
			return -1;
		}

		LM_DBG("looking for To: %s Contact: %s\n", to_str, contact_str);

		/* rule exists in allow file */
		if(search_rule(perm_allow[idx].rules, to_str, contact_str)) {
			if(perm_check_all_branches)
				goto skip_deny;
		}

		/* rule exists in deny file */
		if(search_rule(perm_deny[idx].rules, to_str, contact_str)) {
			LM_DBG("deny rule found => Register denied\n");
			return -1;
		}

	skip_deny:
		if(contact_iterator(&c, msg, c) < 0) {
			return -1;
		}
	}

	LM_DBG("no contact denied => Allowed\n");
	return 1;
}


int allow_register_1(struct sip_msg *msg, char *basename, char *s)
{
	return check_register(msg, (int)(long)basename, 0);
}

int allow_register_2(struct sip_msg *msg, char *allow_file, char *deny_file)
{
	return check_register(msg, (int)(long)allow_file, 0);
}

int allow_register_include_port_1(struct sip_msg *msg, char *basename, char *s)
{
	return check_register(msg, (int)(long)basename, 1);
}

int allow_register_include_port_2(
		struct sip_msg *msg, char *allow_file, char *deny_file)
{
	return check_register(msg, (int)(long)allow_file, 1);
}


/*
 * determines the permission to an uri
 * return values:
 * -1:	deny
 * 1:	allow
 */
static int allow_uri(struct sip_msg *msg, char *_idx, char *_sp)
{
	struct hdr_field *from;
	int idx, len;
	static char from_str[EXPRESSION_LENGTH + 1];
	static char uri_str[EXPRESSION_LENGTH + 1];
	pv_spec_t *sp;
	pv_value_t pv_val;

	idx = (int)(long)_idx;
	sp = (pv_spec_t *)_sp;

	/* turn off control, allow any uri */
	if((!perm_allow[idx].rules) && (!perm_deny[idx].rules)) {
		LM_DBG("no rules => allow any uri\n");
		return 1;
	}

	/* looking for FROM HF */
	if((!msg->from) && (parse_headers(msg, HDR_FROM_F, 0) == -1)) {
		LM_ERR("failed to parse message\n");
		return -1;
	}

	if(!msg->from) {
		LM_ERR("FROM header field not found\n");
		return -1;
	}

	/* we must call parse_from_header explicitly */
	if((!(msg->from)->parsed) && (parse_from_header(msg) < 0)) {
		LM_ERR("failed to parse From body\n");
		return -1;
	}

	from = msg->from;
	len = ((struct to_body *)from->parsed)->uri.len;
	if(len > EXPRESSION_LENGTH) {
		LM_ERR("From header field is too long: %d chars\n", len);
		return -1;
	}
	strncpy(from_str, ((struct to_body *)from->parsed)->uri.s, len);
	from_str[len] = '\0';

	if(sp && (pv_get_spec_value(msg, sp, &pv_val) == 0)) {
		if(pv_val.flags & PV_VAL_STR) {
			if(pv_val.rs.len > EXPRESSION_LENGTH) {
				LM_ERR("pseudo variable value is too "
					   "long: %d chars\n",
						pv_val.rs.len);
				return -1;
			}
			strncpy(uri_str, pv_val.rs.s, pv_val.rs.len);
			uri_str[pv_val.rs.len] = '\0';
		} else {
			LM_ERR("pseudo variable value is not string\n");
			return -1;
		}
	} else {
		LM_ERR("cannot get pseudo variable value\n");
		return -1;
	}

	LM_DBG("looking for From: %s URI: %s\n", from_str, uri_str);
	/* rule exists in allow file */
	if(search_rule(perm_allow[idx].rules, from_str, uri_str)) {
		LM_DBG("allow rule found => URI is allowed\n");
		return 1;
	}

	/* rule exists in deny file */
	if(search_rule(perm_deny[idx].rules, from_str, uri_str)) {
		LM_DBG("deny rule found => URI is denied\n");
		return -1;
	}

	LM_DBG("neither allow nor deny rule found => URI is allowed\n");

	return 1;
}


/*
 * Test URI against Contact.
 */
int allow_test(char *file, char *uri, char *contact)
{
	char *pathname;
	int idx;

	pathname = get_pathname(file);
	if(!pathname) {
		LM_ERR("Cannot get pathname of <%s>\n", file);
		return 0;
	}

	idx = find_index(perm_allow, pathname);
	if(idx == -1) {
		LM_ERR("File <%s> has not been loaded\n", pathname);
		pkg_free(pathname);
		return 0;
	}

	pkg_free(pathname);

	/* turn off control, allow any routing */
	if((!perm_allow[idx].rules) && (!perm_deny[idx].rules)) {
		LM_DBG("No rules => Allowed\n");
		return 1;
	}

	LM_DBG("Looking for URI: %s, Contact: %s\n", uri, contact);

	/* rule exists in allow file */
	if(search_rule(perm_allow[idx].rules, uri, contact)) {
		LM_DBG("Allow rule found => Allowed\n");
		return 1;
	}

	/* rule exists in deny file */
	if(search_rule(perm_deny[idx].rules, uri, contact)) {
		LM_DBG("Deny rule found => Denied\n");
		return 0;
	}

	LM_DBG("Neither allow or deny rule found => Allowed\n");
	return 1;
}

/* clang-format off */
static const char *rpc_trusted_reload_doc[2] = {
	"Reload permissions trusted table",
	0
};

static const char *rpc_address_reload_doc[2] = {
	"Reload permissions address table",
	0
};

static const char *rpc_trusted_dump_doc[2] = {
	"Dump permissions trusted table",
	0
};

static const char *rpc_address_dump_doc[2] = {
	"Dump permissions address table",
	0
};

static const char *rpc_subnet_dump_doc[2] = {
	"Dump permissions subnet table",
	0
};

static const char *rpc_domain_name_dump_doc[2] = {
	"Dump permissions domain name table",
	0
};

static const char *rpc_test_uri_doc[2] = {
	"Tests if (URI, Contact) pair is allowed according to allow/deny files",
	0
};

rpc_export_t permissions_rpc[] = {
	{"permissions.trustedReload", rpc_trusted_reload, rpc_trusted_reload_doc,
			RPC_EXEC_DELTA},
	{"permissions.addressReload", rpc_address_reload, rpc_address_reload_doc,
			RPC_EXEC_DELTA},
	{"permissions.trustedDump", rpc_trusted_dump, rpc_trusted_dump_doc, 0},
	{"permissions.addressDump", rpc_address_dump, rpc_address_dump_doc,
			RET_ARRAY},
	{"permissions.subnetDump", rpc_subnet_dump, rpc_subnet_dump_doc, RET_ARRAY},
	{"permissions.domainDump", rpc_domain_name_dump,
			rpc_domain_name_dump_doc, 0},
	{"permissions.testUri", rpc_test_uri, rpc_test_uri_doc, 0},
	{"permissions.allowUri", rpc_test_uri, rpc_test_uri_doc, 0},
	{0, 0, 0, 0}
};
/* clang-format on */

static int permissions_init_rpc(void)
{
	if(rpc_register_array(permissions_rpc) != 0) {
		LM_ERR("failed to register RPC commands\n");
		return -1;
	}
	return 0;
}

/**
 *
 */
/* clang-format off */
static sr_kemi_t sr_kemi_permissions_exports[] = {
	{ str_init("permissions"), str_init("allow_source_address"),
		SR_KEMIP_INT, allow_source_address,
		{ SR_KEMIP_INT, SR_KEMIP_NONE, SR_KEMIP_NONE,
			SR_KEMIP_NONE, SR_KEMIP_NONE, SR_KEMIP_NONE }
	},
	{ str_init("permissions"), str_init("allow_address"),
		SR_KEMIP_INT, allow_address,
		{ SR_KEMIP_INT, SR_KEMIP_STR, SR_KEMIP_INT,
			SR_KEMIP_NONE, SR_KEMIP_NONE, SR_KEMIP_NONE }
	},
	{ str_init("permissions"), str_init("allow_source_address_group"),
		SR_KEMIP_INT, ki_allow_source_address_group,
		{ SR_KEMIP_NONE, SR_KEMIP_NONE, SR_KEMIP_NONE,
			SR_KEMIP_NONE, SR_KEMIP_NONE, SR_KEMIP_NONE }
	},
	{ str_init("permissions"), str_init("allow_address_group"),
		SR_KEMIP_INT, ki_allow_address_group,
		{ SR_KEMIP_STR, SR_KEMIP_INT, SR_KEMIP_NONE,
			SR_KEMIP_NONE, SR_KEMIP_NONE, SR_KEMIP_NONE }
	},
	{ str_init("permissions"), str_init("allow_trusted"),
		SR_KEMIP_INT, ki_allow_trusted,
		{ SR_KEMIP_NONE, SR_KEMIP_NONE, SR_KEMIP_NONE,
			SR_KEMIP_NONE, SR_KEMIP_NONE, SR_KEMIP_NONE }
	},

	{ {0, 0}, {0, 0}, 0, NULL, { 0, 0, 0, 0, 0, 0 } }
};
/* clang-format on */

/**
 *
 */
int mod_register(char *path, int *dlflags, void *p1, void *p2)
{
	sr_kemi_modules_add(sr_kemi_permissions_exports);
	return 0;
}
