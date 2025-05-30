/*
 * pua_dialoginfo module - publish dialog-info from dialog module
 *
 * Copyright (C) 2006 Voice Sistem S.R.L.
 * Copyright (C) 2007-2008 Dan Pascu
 * Copyright (C) 2008 Klaus Darilion IPCom
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
#include <stdlib.h>
#include <string.h>
#include <libxml/parser.h>
#include <time.h>

#include "../../core/script_cb.h"
#include "../../core/sr_module.h"
#include "../../core/parser/parse_expires.h"
#include "../../core/dprint.h"
#include "../../core/mem/shm_mem.h"
#include "../../core/parser/msg_parser.h"
#include "../../core/parser/parse_to.h"
#include "../../core/parser/contact/parse_contact.h"
#include "../../core/str.h"
#include "../../core/str_list.h"
#include "../../core/mem/mem.h"
#include "../../core/pt.h"
#include "../../core/ut.h"
#include "../../core/utils/sruid.h"
#include "../dialog/dlg_load.h"
#include "../dialog/dlg_hash.h"
#include "../pua/pua_bind.h"
#include "pua_dialoginfo.h"

MODULE_VERSION

/* Default module parameter values */
#define DEF_USE_UUID 0
#define DEF_INCLUDE_CALLID 1
#define DEF_INCLUDE_LOCALREMOTE 1
#define DEF_INCLUDE_TAGS 1
#define DEF_OVERRIDE_LIFETIME 0
#define DEF_CALLER_ALWAYS_CONFIRMED 0
#define DEF_INCLUDE_REQ_URI 0
#define DEF_OVERRIDE_LIFETIME 0
#define DEF_SEND_PUBLISH_FLAG -1
#define DEF_USE_PUBRURI_AVPS 0
#define DEF_REFRESH_PUBRURI_AVPS_FLAG -1
#define DEF_PUBRURI_CALLER_AVP 0
#define DEF_PUBRURI_CALLEE_AVP 0
#define DEF_CALLEE_TRYING 0
#define DEF_DISABLE_CALLER_PUBLISH_FLAG -1
#define DEF_DISABLE_CALLEE_PUBLISH_FLAG -1
#define DEF_PUBLISH_DIALOG_REQ_WITHIN 1

/* define PUA_DIALOGINFO_DEBUG to activate more verbose
 * logging and dialog info callback debugging
 */
/* #define PUA_DIALOGINFO_DEBUG 1 */

pua_api_t _pua_api;

struct dlg_binds dlg_api;

avp_flags_t pubruri_caller_avp_type;
avp_name_t pubruri_caller_avp_name;
avp_flags_t pubruri_callee_avp_type;
avp_name_t pubruri_callee_avp_name;
sruid_t _puadi_sruid;

static char *DLG_VAR_SEP = ",";
static str caller_dlg_var = {0, 0};						 /* pubruri_caller */
static str callee_dlg_var = {0, 0};						 /* pubruri_callee */
static str caller_entity_when_publish_disabled = {0, 0}; /* pubruri_caller */
static str callee_entity_when_publish_disabled = {0, 0}; /* pubruri_callee */
static str local_identity_dlg_var = STR_NULL;

/* Module parameter variables */
int use_uuid = DEF_USE_UUID;
int include_callid = DEF_INCLUDE_CALLID;
int include_localremote = DEF_INCLUDE_LOCALREMOTE;
int include_tags = DEF_INCLUDE_TAGS;
int override_lifetime = DEF_OVERRIDE_LIFETIME;
int caller_confirmed = DEF_CALLER_ALWAYS_CONFIRMED;
int include_req_uri = DEF_INCLUDE_REQ_URI;
int send_publish_flag = DEF_SEND_PUBLISH_FLAG;
int use_pubruri_avps = DEF_USE_PUBRURI_AVPS;
int refresh_pubruri_avps_flag = DEF_REFRESH_PUBRURI_AVPS_FLAG;
int callee_trying = DEF_CALLEE_TRYING;
int disable_caller_publish_flag = DEF_DISABLE_CALLER_PUBLISH_FLAG;
int disable_callee_publish_flag = DEF_DISABLE_CALLEE_PUBLISH_FLAG;
char *pubruri_caller_avp = DEF_PUBRURI_CALLER_AVP;
char *pubruri_callee_avp = DEF_PUBRURI_CALLEE_AVP;
int publish_dialog_req_within = DEF_PUBLISH_DIALOG_REQ_WITHIN;
int dialog_event_types = DLGCB_FAILED | DLGCB_CONFIRMED_NA | DLGCB_TERMINATED
						 | DLGCB_EXPIRED | DLGCB_EARLY;

int puadinfo_attribute_display = 0;

send_publish_t pua_send_publish;
/** module functions */

static int mod_init(void);
static int child_init(int rank);

/* clang-format off */
static cmd_export_t cmds[] = {
	{0, 0, 0, 0, 0, 0}
};

static param_export_t params[] = {
	{"use_uuid", PARAM_INT, &use_uuid},
	{"include_callid", PARAM_INT, &include_callid},
	{"include_localremote", PARAM_INT, &include_localremote},
	{"include_tags", PARAM_INT, &include_tags},
	{"override_lifetime", PARAM_INT, &override_lifetime},
	{"caller_confirmed", PARAM_INT, &caller_confirmed},
	{"include_req_uri", PARAM_INT, &include_req_uri},
	{"send_publish_flag", PARAM_INT, &send_publish_flag},
	{"use_pubruri_avps", PARAM_INT, &use_pubruri_avps},
	{"refresh_pubruri_avps_flag", PARAM_INT, &refresh_pubruri_avps_flag},
	{"pubruri_caller_avp", PARAM_STRING, &pubruri_caller_avp},
	{"pubruri_callee_avp", PARAM_STRING, &pubruri_callee_avp},
	{"pubruri_caller_dlg_var", PARAM_STR, &caller_dlg_var},
	{"pubruri_callee_dlg_var", PARAM_STR, &callee_dlg_var},
	{"local_identity_dlg_var", PARAM_STR, &local_identity_dlg_var},
	{"callee_trying", PARAM_INT, &callee_trying},
	{"disable_caller_publish_flag", PARAM_INT, &disable_caller_publish_flag},
	{"disable_callee_publish_flag", PARAM_INT, &disable_callee_publish_flag},
	{"caller_entity_when_publish_disabled", PARAM_STR, &caller_entity_when_publish_disabled},
	{"callee_entity_when_publish_disabled", PARAM_STR, &callee_entity_when_publish_disabled},
	{"publish_dialog_req_within", PARAM_INT, &publish_dialog_req_within},
	{"attribute_display", PARAM_INT, &puadinfo_attribute_display},
	{0, 0, 0}
};

struct module_exports exports = {
	"pua_dialoginfo", /* module name */
	DEFAULT_DLFLAGS,  /* dlopen flags */
	cmds,			  /* exported functions */
	params,			  /* exported parameters */
	0,				  /* RPC method exports */
	0,				  /* exported pseudo-variables */
	0,				  /* response handling function */
	mod_init,		  /* module initialization function */
	child_init,		  /* per-child init function */
	0				  /* module destroy function */
};
/* clang-format on */


#ifdef PUA_DIALOGINFO_DEBUG
static void __dialog_cbtest(
		struct dlg_cell *dlg, int type, struct dlg_cb_params *_params)
{
	str tag;
	struct sip_msg *msg;
	LM_ERR("dialog callback received, from=%.*s, to=%.*s\n", dlg->from_uri.len,
			dlg->from_uri.s, dlg->to_uri.len, dlg->to_uri.s);
	if(dlg->tag[0].len && dlg->tag[0].s) {
		LM_ERR("dialog callback: tag[0] = %.*s", dlg->tag[0].len,
				dlg->tag[0].s);
	}
	if(dlg->tag[0].len && dlg->tag[1].s) {
		LM_ERR("dialog callback: tag[1] = %.*s", dlg->tag[1].len,
				dlg->tag[1].s);
	}

	if(type != DLGCB_DESTROY) {
		msg = dlg_get_valid_msg(_params);
		if(!msg) {
			LM_ERR("no SIP message available in callback parameters\n");
			return;
		}

		/* get to tag*/
		if(!msg->to) {
			// to header not defined, parse to header
			LM_ERR("to header not defined, parse to header\n");
			if(parse_headers(msg, HDR_TO_F, 0) < 0) {
				//parser error
				LM_ERR("parsing of to-header failed\n");
				tag.s = 0;
				tag.len = 0;
			} else if(!msg->to) {
				// to header still not defined
				LM_ERR("bad reply or missing TO header\n");
				tag.s = 0;
				tag.len = 0;
			} else {
				tag = get_to(msg)->tag_value;
			}
		} else {
			tag = get_to(msg)->tag_value;
			if(tag.s == 0 || tag.len == 0) {
				LM_ERR("missing TAG param in TO hdr :-/\n");
				tag.s = 0;
				tag.len = 0;
			}
		}
		if(tag.s) {
			LM_ERR("dialog callback: msg->to->parsed->tag_value = %.*s",
					tag.len, tag.s);
		}
	}

	switch(type) {
		case DLGCB_FAILED:
			LM_ERR("dialog callback type 'DLGCB_FAILED' received, from=%.*s\n",
					dlg->from_uri.len, dlg->from_uri.s);
			break;
		case DLGCB_CONFIRMED_NA:
			LM_ERR("dialog callback type 'DLGCB_CONFIRMED_NA' received, "
				   "from=%.*s\n",
					dlg->from_uri.len, dlg->from_uri.s);
			break;
		case DLGCB_CONFIRMED:
			LM_ERR("dialog callback type 'DLGCB_CONFIRMED' received, "
				   "from=%.*s\n",
					dlg->from_uri.len, dlg->from_uri.s);
			break;
		case DLGCB_REQ_WITHIN:
			LM_ERR("dialog callback type 'DLGCB_REQ_WITHIN' received, "
				   "from=%.*s\n",
					dlg->from_uri.len, dlg->from_uri.s);
			break;
		case DLGCB_TERMINATED:
			LM_ERR("dialog callback type 'DLGCB_TERMINATED' received, "
				   "from=%.*s\n",
					dlg->from_uri.len, dlg->from_uri.s);
			break;
		case DLGCB_EXPIRED:
			LM_ERR("dialog callback type 'DLGCB_EXPIRED' received, from=%.*s\n",
					dlg->from_uri.len, dlg->from_uri.s);
			break;
		case DLGCB_EARLY:
			LM_ERR("dialog callback type 'DLGCB_EARLY' received, from=%.*s\n",
					dlg->from_uri.len, dlg->from_uri.s);
			break;
		case DLGCB_RESPONSE_FWDED:
			LM_ERR("dialog callback type 'DLGCB_RESPONSE_FWDED' received, "
				   "from=%.*s\n",
					dlg->from_uri.len, dlg->from_uri.s);
			break;
		case DLGCB_RESPONSE_WITHIN:
			LM_ERR("dialog callback type 'DLGCB_RESPONSE_WITHIN' received, "
				   "from=%.*s\n",
					dlg->from_uri.len, dlg->from_uri.s);
			break;
		case DLGCB_DESTROY:
			LM_ERR("dialog callback type 'DLGCB_DESTROY' received, from=%.*s\n",
					dlg->from_uri.len, dlg->from_uri.s);
			break;
		default:
			LM_ERR("dialog callback type 'unknown' received, from=%.*s\n",
					dlg->from_uri.len, dlg->from_uri.s);
	}
}
#endif

static struct str_list *get_str_list(
		unsigned short avp_flags, int_str avp_name);
static int is_ruri_in_list(struct str_list *list, str *ruri);

void refresh_pubruri_avps(struct dlginfo_cell *dlginfo, str *uri)
{
	struct str_list *pubruris =
			get_str_list(pubruri_caller_avp_type, pubruri_caller_avp_name);
	struct str_list *list, *next;
	str target = STR_NULL;

	if(pubruris) {
		list = dlginfo->pubruris_caller;
		while(list) {
			if(is_ruri_in_list(pubruris, &list->s) == 0) {
				LM_DBG("ruri:'%.*s' removed from pubruris_caller list\n",
						list->s.len, list->s.s);
				next = list->next;
				list->next = NULL;
				dialog_publish_multi("terminated", list, &(dlginfo->from_uri),
						uri, &(dlginfo->callid), 1, 10, 0, 0,
						&(dlginfo->from_contact), &target,
						send_publish_flag == -1 ? 1 : 0, &(dlginfo->uuid));
				list->next = next;
			}
			list = list->next;
		}
		free_str_list_all(dlginfo->pubruris_caller);
		dlginfo->pubruris_caller = pubruris;
		LM_DBG("refreshed pubruris_caller info from avp\n");
	}
	pubruris = get_str_list(pubruri_callee_avp_type, pubruri_callee_avp_name);
	if(pubruris) {
		list = dlginfo->pubruris_callee;
		while(list) {
			if(is_ruri_in_list(pubruris, &list->s) == 0) {
				LM_DBG("ruri:'%.*s' removed from pubruris_callee list\n",
						list->s.len, list->s.s);
				next = list->next;
				list->next = NULL;
				dialog_publish_multi("terminated", list, uri,
						&(dlginfo->from_uri), &(dlginfo->callid), 0, 10, 0, 0,
						&target, &(dlginfo->from_contact),
						send_publish_flag == -1 ? 1 : 0, &(dlginfo->uuid));
				list->next = next;
			}
			list = list->next;
		}
		free_str_list_all(dlginfo->pubruris_callee);
		dlginfo->pubruris_callee = pubruris;
		LM_DBG("refreshed pubruris_callee info from avp\n");
	}
}

void refresh_local_identity(struct dlg_cell *dlg, str *uri)
{
	str s = {0};

	dlg_api.get_dlg_varval(dlg, &local_identity_dlg_var, &s);

	if(s.s != NULL) {
		uri->s = s.s;
		uri->len = s.len;
		LM_DBG("Found local_identity in dialog '%.*s'\n", uri->len, uri->s);
	}
}

static void __dialog_sendpublish(
		struct dlg_cell *dlg, int type, struct dlg_cb_params *_params)
{
	str tag = {0, 0};
	str uri = {0, 0};
	str identity_local = {0, 0};
	str target = {0, 0};
	struct dlginfo_cell *dlginfo = NULL;

	struct sip_msg *request = _params->req;
	dlginfo = (struct dlginfo_cell *)*_params->param;

	if(dlg == NULL || dlginfo == NULL) {
		LM_WARN("execution with null parameters - type %d, dlg %p, info %p\n",
				type, dlg, dlginfo);
		return;
	}

	/* skip requests that do not control call state */
	if(request && (request->REQ_METHOD) & (METHOD_PRACK | METHOD_UPDATE)) {
		return;
	}
	if(include_req_uri) {
		uri = dlginfo->req_uri;
	} else {
		uri = dlginfo->to_uri;
	}

	if(dlginfo->disable_caller_publish) {
		identity_local = caller_entity_when_publish_disabled;
	} else {
		identity_local = dlginfo->from_uri;
	}

	if(dlginfo->disable_callee_publish) {
		uri = callee_entity_when_publish_disabled;
	}

	if(use_pubruri_avps && (refresh_pubruri_avps_flag > -1) && (request != NULL)
			&& (request->flags
					& (1U << (unsigned int)refresh_pubruri_avps_flag))) {
		lock_get(&dlginfo->lock);
		refresh_pubruri_avps(dlginfo, &uri);
	}

	if(local_identity_dlg_var.len > 0) {
		refresh_local_identity(dlg, &uri);
	}

	switch(type) {
		case DLGCB_FAILED:
		case DLGCB_TERMINATED:
		case DLGCB_EXPIRED:
			LM_DBG("dialog over, from=%.*s\n", dlginfo->from_uri.len,
					dlginfo->from_uri.s);
			if((!dlginfo->disable_caller_publish)
					&& (disable_caller_publish_flag == -1
							|| !(request
									&& (request->flags
											& (1 << disable_caller_publish_flag))))) {
				dialog_publish_multi("terminated", dlginfo->pubruris_caller,
						&identity_local, &uri, &(dlginfo->callid), 1, 10, 0, 0,
						&(dlginfo->from_contact), &target,
						send_publish_flag == -1 ? 1 : 0, &(dlginfo->uuid));
			}
			if((!dlginfo->disable_callee_publish)
					&& (disable_callee_publish_flag == -1
							|| !(request
									&& (request->flags
											& (1 << disable_callee_publish_flag))))) {
				dialog_publish_multi("terminated", dlginfo->pubruris_callee,
						&uri, &identity_local, &(dlginfo->callid), 0, 10, 0, 0,
						&target, &(dlginfo->from_contact),
						send_publish_flag == -1 ? 1 : 0, &(dlginfo->uuid));
			}
			break;
		case DLGCB_CONFIRMED:
		case DLGCB_REQ_WITHIN:
		case DLGCB_CONFIRMED_NA:
			LM_DBG("dialog confirmed, from=%.*s\n", dlginfo->from_uri.len,
					dlginfo->from_uri.s);
			if((!dlginfo->disable_caller_publish)
					&& (disable_caller_publish_flag == -1
							|| !(request
									&& (request->flags
											& (1 << disable_caller_publish_flag))))) {
				dialog_publish_multi("confirmed", dlginfo->pubruris_caller,
						&identity_local, &uri, &(dlginfo->callid), 1,
						dlginfo->lifetime, 0, 0, &(dlginfo->from_contact),
						&target, send_publish_flag == -1 ? 1 : 0,
						&(dlginfo->uuid));
			}
			if((!dlginfo->disable_callee_publish)
					&& (disable_callee_publish_flag == -1
							|| !(request
									&& (request->flags
											& (1 << disable_callee_publish_flag))))) {
				dialog_publish_multi("confirmed", dlginfo->pubruris_callee,
						&uri, &identity_local, &(dlginfo->callid), 0,
						dlginfo->lifetime, 0, 0, &target,
						&(dlginfo->from_contact),
						send_publish_flag == -1 ? 1 : 0, &(dlginfo->uuid));
			}
			break;
		case DLGCB_EARLY:
			LM_DBG("dialog is early, from=%.*s\n", dlginfo->from_uri.len,
					dlginfo->from_uri.s);
			if(include_tags) {
				/* get remotetarget */
				if(!_params->rpl->contact
						&& ((parse_headers(_params->rpl, HDR_CONTACT_F, 0) < 0)
								|| !_params->rpl->contact)) {
					LM_ERR("bad reply or missing CONTACT hdr\n");
				} else {
					if(parse_contact(_params->rpl->contact) < 0
							|| ((contact_body_t *)_params->rpl->contact->parsed)
											   ->contacts
									   == NULL
							|| ((contact_body_t *)_params->rpl->contact->parsed)
											   ->contacts->next
									   != NULL) {
						LM_ERR("Malformed CONTACT hdr\n");
					} else {
						target = ((contact_body_t *)
										  _params->rpl->contact->parsed)
										 ->contacts->uri;
					}
				}
				/* get to tag*/
				if(!_params->rpl->to
						&& ((parse_headers(_params->rpl, HDR_TO_F, 0) < 0)
								|| !_params->rpl->to)) {
					LM_ERR("bad reply or missing TO hdr :-/\n");
					tag.s = 0;
					tag.len = 0;
				} else {
					tag = get_to(_params->rpl)->tag_value;
					if(tag.s == 0 || tag.len == 0) {
						LM_ERR("missing TAG param in TO hdr :-/\n");
						tag.s = 0;
						tag.len = 0;
					}
				}
				if((!dlginfo->disable_caller_publish)
						&& (disable_caller_publish_flag == -1
								|| !(request
										&& (request->flags
												& (1 << disable_caller_publish_flag))))) {
					if(caller_confirmed) {
						dialog_publish_multi("confirmed",
								dlginfo->pubruris_caller, &identity_local, &uri,
								&(dlginfo->callid), 1, dlginfo->lifetime,
								&(dlginfo->from_tag), &tag,
								&(dlginfo->from_contact), &target,
								send_publish_flag == -1 ? 1 : 0,
								&(dlginfo->uuid));
					} else {
						dialog_publish_multi("early", dlginfo->pubruris_caller,
								&identity_local, &uri, &(dlginfo->callid), 1,
								dlginfo->lifetime, &(dlginfo->from_tag), &tag,
								&(dlginfo->from_contact), &target,
								send_publish_flag == -1 ? 1 : 0,
								&(dlginfo->uuid));
					}
				}
				if((!dlginfo->disable_callee_publish)
						&& (disable_callee_publish_flag == -1
								|| !(request
										&& (request->flags
												& (1 << disable_callee_publish_flag))))) {
					dialog_publish_multi("early", dlginfo->pubruris_callee,
							&uri, &identity_local, &(dlginfo->callid), 0,
							dlginfo->lifetime, &tag, &(dlginfo->from_tag),
							&target, &(dlginfo->from_contact),
							send_publish_flag == -1 ? 1 : 0, &(dlginfo->uuid));
				}

			} else {
				if((!dlginfo->disable_caller_publish)
						&& (disable_caller_publish_flag == -1
								|| !(request
										&& (request->flags
												& (1 << disable_caller_publish_flag))))) {
					if(caller_confirmed) {
						dialog_publish_multi("confirmed",
								dlginfo->pubruris_caller, &identity_local, &uri,
								&(dlginfo->callid), 1, dlginfo->lifetime, 0, 0,
								&(dlginfo->from_contact), &target,
								send_publish_flag == -1 ? 1 : 0,
								&(dlginfo->uuid));

					} else {
						dialog_publish_multi("early", dlginfo->pubruris_caller,
								&identity_local, &uri, &(dlginfo->callid), 1,
								dlginfo->lifetime, 0, 0,
								&(dlginfo->from_contact), &target,
								send_publish_flag == -1 ? 1 : 0,
								&(dlginfo->uuid));
					}
				}
				if((!dlginfo->disable_callee_publish)
						&& (disable_callee_publish_flag == -1
								|| !(request
										&& (request->flags
												& (1 << disable_callee_publish_flag))))) {
					dialog_publish_multi("early", dlginfo->pubruris_callee,
							&uri, &identity_local, &(dlginfo->callid), 0,
							dlginfo->lifetime, 0, 0, &target,
							&(dlginfo->from_contact),
							send_publish_flag == -1 ? 1 : 0, &(dlginfo->uuid));
				}
			}
			break;
		default:
			LM_ERR("unhandled dialog callback type %d received, from=%.*s\n",
					type, dlginfo->from_uri.len, dlginfo->from_uri.s);
			if((!dlginfo->disable_caller_publish)
					&& (disable_caller_publish_flag == -1
							|| !(request
									&& (request->flags
											& (1 << disable_caller_publish_flag))))) {
				dialog_publish_multi("terminated", dlginfo->pubruris_caller,
						&identity_local, &uri, &(dlginfo->callid), 1, 10, 0, 0,
						&(dlginfo->from_contact), &target,
						send_publish_flag == -1 ? 1 : 0, &(dlginfo->uuid));
			}
			if((!dlginfo->disable_callee_publish)
					&& (disable_callee_publish_flag == -1
							|| !(request
									&& (request->flags
											& (1 << disable_callee_publish_flag))))) {
				dialog_publish_multi("terminated", dlginfo->pubruris_callee,
						&uri, &identity_local, &(dlginfo->callid), 0, 10, 0, 0,
						&target, &(dlginfo->from_contact),
						send_publish_flag == -1 ? 1 : 0, &(dlginfo->uuid));
			}
	}

	if(use_pubruri_avps && (refresh_pubruri_avps_flag > -1) && (request != NULL)
			&& (request->flags
					& (1U << (unsigned int)refresh_pubruri_avps_flag))) {
		lock_release(&dlginfo->lock);
	}
}

/*
 *  Writes all avps with name avp_name to new str_list (shm mem)
 *  Be careful: returns NULL pointer if no avp present!
 *
 */
struct str_list *get_str_list(unsigned short avp_flags, int_str avp_name)
{

	int_str avp_value;
	struct str_list *list_first = 0;
	struct str_list *list_current = 0;
	struct search_state st;

	if(!search_first_avp(avp_flags, avp_name, &avp_value, &st)) {
		return NULL;
	}

	do {
		LM_DBG("AVP found '%.*s'\n", avp_value.s.len, avp_value.s.s);
		if(list_current) {
			list_current->next =
					(struct str_list *)shm_malloc(sizeof(struct str_list));
			list_current = list_current->next;
		} else {
			list_current = list_first =
					(struct str_list *)shm_malloc(sizeof(struct str_list));
		}

		if(!list_current) {
			SHM_MEM_ERROR;
			free_str_list_all(list_first);
			return NULL;
		}
		memset(list_current, 0, sizeof(struct str_list));
		list_current->s.s = shm_str2char_dup(&avp_value.s);
		if(!list_current->s.s) {
			free_str_list_all(list_first);
			return NULL;
		}
		list_current->s.len = avp_value.s.len;
	} while(search_next_avp(&st, &avp_value));

	return list_first;
}

/**
 * @brief set dlg_var value from str_list as comma separated values
 *
 * @param dlg dialog
 * @param key dlg_var keyname
 * @param lst list of str values
 * @return int
 */
static int set_dlg_var(struct dlg_cell *dlg, str *key, struct str_list *lst)
{
	str buf = STR_NULL;
	struct str_list *it = lst;
	int num = -1;
	int res;

	if(!lst)
		return -1;

	while(it) {
		buf.len += it->s.len + ++num;
		it = it->next;
	}
	buf.s = (char *)pkg_malloc(sizeof(char) * buf.len);

	it = lst;
	num = 0;
	while(it) {
		memcpy(buf.s + num, it->s.s, it->s.len);
		if(it->next) {
			num += it->s.len;
			buf.s[num++] = *DLG_VAR_SEP;
		}
		it = it->next;
	}
	res = dlg_api.set_dlg_var(dlg, key, &buf);
	pkg_free(buf.s);

	return res;
}

static int get_dlg_var(struct dlg_cell *dlg, str *key, struct str_list **lst)
{
	str dval = STR_NULL;
	str val = STR_NULL;
	struct str_list *it, *prev;
	char *sep, *ini, *end;

	if(dlg_api.get_dlg_varval(dlg, &caller_dlg_var, &dval) != 0
			|| dval.s == NULL)
		return 0;

	if(*lst) {
		free_str_list_all(*lst);
	}
	*lst = prev = NULL;
	ini = dval.s;
	end = dval.s + dval.len - 1;
	sep = stre_search_strz(ini, end, DLG_VAR_SEP);
	if(!sep)
		sep = end;
	do {
		val.s = ini;
		val.len = sep - ini + 1;
		ini = sep + 1;
		it = (struct str_list *)shm_malloc(sizeof(struct str_list));
		if(!it) {
			SHM_MEM_ERROR;
			return -1;
		}
		memset(it, 0, sizeof(struct str_list));
		it->s.s = shm_str2char_dup(&val);
		if(!it->s.s) {
			free_str_list_all(*lst);
			return -1;
		}
		it->s.len = val.len;
		LM_DBG("Found uri '%.*s' in dlg_var:'%.*s'\n", val.len, val.s, key->len,
				key->s);
		if(!*lst) {
			*lst = prev = it;
		} else {
			prev->next = it;
		}
		if(ini < end)
			sep = stre_search_strz(ini, end, DLG_VAR_SEP);
		else
			sep = NULL;
	} while(sep);

	return 0;
}

struct dlginfo_cell *get_dialog_data(struct dlg_cell *dlg, int type,
		int disable_caller_publish, int disable_callee_publish)
{
	struct dlginfo_cell *dlginfo;
	int len;

	// generate new random uuid
	if(use_uuid) {
		_puadi_sruid.uid.len = SRUID_SIZE;
		if(sruid_uuid_generate(_puadi_sruid.uid.s, &_puadi_sruid.uid.len) < 0) {
			LM_ERR("uuid not generated\n");
			return NULL;
		}
	} else {
		if(sruid_next_safe(&_puadi_sruid) < 0) {
			return NULL;
		}
	}
	LM_DBG("uuid generated: '%.*s'\n", _puadi_sruid.uid.len,
			_puadi_sruid.uid.s);

	/* create dlginfo structure to store important data inside the module*/
	len = sizeof(struct dlginfo_cell) + dlg->from_uri.len + dlg->to_uri.len
		  + dlg->callid.len + dlg->tag[0].len + dlg->req_uri.len
		  + dlg->contact[0].len + _puadi_sruid.uid.len;

	dlginfo = (struct dlginfo_cell *)shm_malloc(len);
	if(dlginfo == 0) {
		SHM_MEM_ERROR;
		return NULL;
	}
	memset(dlginfo, 0, len);

	if(use_pubruri_avps && lock_init(&dlginfo->lock) == 0) {
		LM_ERR("cannot init the lock\n");
		free_dlginfo_cell(dlginfo);
		return NULL;
	}

	/* copy from dlg structure to dlginfo structure */
	dlginfo->lifetime = override_lifetime ? override_lifetime : dlg->lifetime;
	dlginfo->disable_caller_publish = disable_caller_publish;
	dlginfo->disable_callee_publish = disable_callee_publish;
	dlginfo->from_uri.s = (char *)dlginfo + sizeof(struct dlginfo_cell);
	dlginfo->from_uri.len = dlg->from_uri.len;
	dlginfo->to_uri.s = dlginfo->from_uri.s + dlg->from_uri.len;
	dlginfo->to_uri.len = dlg->to_uri.len;
	dlginfo->callid.s = dlginfo->to_uri.s + dlg->to_uri.len;
	dlginfo->callid.len = dlg->callid.len;
	dlginfo->from_tag.s = dlginfo->callid.s + dlg->callid.len;
	dlginfo->from_tag.len = dlg->tag[0].len;
	dlginfo->req_uri.s = dlginfo->from_tag.s + dlginfo->from_tag.len;
	dlginfo->req_uri.len = dlg->req_uri.len;
	dlginfo->from_contact.s = dlginfo->req_uri.s + dlginfo->req_uri.len;
	dlginfo->from_contact.len = dlg->contact[0].len;
	dlginfo->uuid.s = dlginfo->from_contact.s + dlginfo->from_contact.len;
	dlginfo->uuid.len = _puadi_sruid.uid.len;

	memcpy(dlginfo->from_uri.s, dlg->from_uri.s, dlg->from_uri.len);
	memcpy(dlginfo->to_uri.s, dlg->to_uri.s, dlg->to_uri.len);
	memcpy(dlginfo->callid.s, dlg->callid.s, dlg->callid.len);
	memcpy(dlginfo->from_tag.s, dlg->tag[0].s, dlg->tag[0].len);
	memcpy(dlginfo->req_uri.s, dlg->req_uri.s, dlg->req_uri.len);
	memcpy(dlginfo->from_contact.s, dlg->contact[0].s, dlg->contact[0].len);
	memcpy(dlginfo->uuid.s, _puadi_sruid.uid.s, _puadi_sruid.uid.len);

	if(use_pubruri_avps) {
		if(type == DLGCB_CREATED) {
			dlginfo->pubruris_caller = get_str_list(
					pubruri_caller_avp_type, pubruri_caller_avp_name);
			dlginfo->pubruris_callee = get_str_list(
					pubruri_callee_avp_type, pubruri_callee_avp_name);

			if(dlginfo->pubruris_callee != NULL && callee_dlg_var.len > 0) {
				if(set_dlg_var(dlg, &callee_dlg_var, dlginfo->pubruris_callee)
						< 0) {
					free_str_list_all(dlginfo->pubruris_callee);
					dlginfo->pubruris_callee = NULL;
				}
			}
			if(dlginfo->pubruris_caller != NULL && caller_dlg_var.len > 0) {
				if(set_dlg_var(dlg, &caller_dlg_var, dlginfo->pubruris_caller)
						< 0) {
					free_str_list_all(dlginfo->pubruris_caller);
					dlginfo->pubruris_caller = NULL;
				}
			}
		} else {
			if(caller_dlg_var.len > 0) {
				if(get_dlg_var(dlg, &caller_dlg_var, &dlginfo->pubruris_caller)
						< 0) {
					free_dlginfo_cell(dlginfo);
					return NULL;
				}
			}

			if(callee_dlg_var.len > 0) {
				if(get_dlg_var(dlg, &callee_dlg_var, &dlginfo->pubruris_callee)
						< 0) {
					free_dlginfo_cell(dlginfo);
					return NULL;
				}
			}
		}

		if(dlginfo->pubruris_caller == 0 && dlginfo->pubruris_callee == 0) {
			/* No reason to save dlginfo, we have nobody to publish to */
			LM_DBG("Neither pubruris_caller nor pubruris_callee found.\n");
			free_dlginfo_cell(dlginfo);
			return NULL;
		}
	} else {
		dlginfo->pubruris_caller =
				(struct str_list *)shm_malloc(sizeof(struct str_list));
		if(dlginfo->pubruris_caller == 0) {
			SHM_MEM_ERROR;
			free_dlginfo_cell(dlginfo);
			return NULL;
		}
		memset(dlginfo->pubruris_caller, 0, sizeof(struct str_list));
		dlginfo->pubruris_caller->s.s = shm_str2char_dup(&dlginfo->from_uri);
		dlginfo->pubruris_caller->s.len = dlginfo->from_uri.len;
		if(!dlginfo->pubruris_caller->s.s) {
			free_dlginfo_cell(dlginfo);
			return NULL;
		}

		dlginfo->pubruris_callee =
				(struct str_list *)shm_malloc(sizeof(struct str_list));
		if(dlginfo->pubruris_callee == 0) {
			SHM_MEM_ERROR;
			free_dlginfo_cell(dlginfo);
			return NULL;
		}
		memset(dlginfo->pubruris_callee, 0, sizeof(struct str_list));

		if(include_req_uri) {
			dlginfo->pubruris_callee->s.s = shm_str2char_dup(&dlginfo->req_uri);
			dlginfo->pubruris_callee->s.len = dlginfo->req_uri.len;
		} else {
			dlginfo->pubruris_callee->s.s = shm_str2char_dup(&dlginfo->to_uri);
			dlginfo->pubruris_callee->s.len = dlginfo->to_uri.len;
		}
	}

	/* register dialog callbacks which triggers sending PUBLISH */
	if(dlg_api.register_dlgcb(dlg, dialog_event_types, __dialog_sendpublish,
			   dlginfo, free_dlginfo_cell)
			!= 0) {
		LM_ERR("cannot register callback for interesting dialog types\n");
		free_dlginfo_cell(dlginfo);
		return NULL;
	}

#ifdef PUA_DIALOGINFO_DEBUG
	/* dialog callback testing (registered last to be executed first) */
	if(dlg_api.register_dlgcb(dlg,
			   DLGCB_FAILED | DLGCB_CONFIRMED_NA | DLGCB_CONFIRMED
					   | DLGCB_REQ_WITHIN | DLGCB_TERMINATED | DLGCB_EXPIRED
					   | DLGCB_EARLY | DLGCB_RESPONSE_FWDED
					   | DLGCB_RESPONSE_WITHIN | DLGCB_DESTROY,
			   __dialog_cbtest, NULL, NULL)
			!= 0) {
		LM_ERR("cannot register callback for all dialog types\n");
		free_dlginfo_cell(dlginfo);
		return NULL;
	}
#endif

	return (dlginfo);
}

static void __dialog_created(
		struct dlg_cell *dlg, int type, struct dlg_cb_params *_params)
{
	struct sip_msg *request = _params->req;
	struct dlginfo_cell *dlginfo;
	str identity_remote = {0, 0};
	str identity_local = {0, 0};
	int disable_caller_publish = 0;
	int disable_callee_publish = 0;

	if(request == NULL || request->REQ_METHOD != METHOD_INVITE)
		return;

	if(send_publish_flag > -1 && !(request->flags & (1 << send_publish_flag)))
		return;

	LM_DBG("new INVITE dialog created: from=%.*s\n", dlg->from_uri.len,
			dlg->from_uri.s);

	if(disable_caller_publish_flag != -1
			&& caller_entity_when_publish_disabled.len > 0
			&& (request
					&& (request->flags & (1 << disable_caller_publish_flag)))) {
		disable_caller_publish = 1;
	}

	if(disable_callee_publish_flag != -1
			&& callee_entity_when_publish_disabled.len > 0
			&& (request
					&& (request->flags & (1 << disable_callee_publish_flag)))) {
		disable_callee_publish = 1;
	}

	dlginfo = get_dialog_data(
			dlg, type, disable_caller_publish, disable_callee_publish);
	if(dlginfo == NULL)
		return;

	if(disable_caller_publish) {
		identity_local = caller_entity_when_publish_disabled;
	} else {
		identity_local = dlginfo->from_uri;
	}

	if(disable_callee_publish) {
		identity_remote = callee_entity_when_publish_disabled;
	} else {
		identity_remote = (include_req_uri) ? dlg->req_uri : dlg->to_uri;
	}

	if((!disable_caller_publish)
			&& (disable_caller_publish_flag == -1
					|| !(request
							&& (request->flags
									& (1 << disable_caller_publish_flag))))) {
		if(use_pubruri_avps)
			lock_get(&dlginfo->lock);
		dialog_publish_multi("Trying", dlginfo->pubruris_caller,
				&identity_local, &identity_remote, &(dlg->callid), 1,
				dlginfo->lifetime, 0, 0, 0, 0,
				(send_publish_flag == -1) ? 1 : 0, &(dlginfo->uuid));
		if(use_pubruri_avps)
			lock_release(&dlginfo->lock);
	}

	if(callee_trying
			&& ((!disable_callee_publish)
					&& (disable_callee_publish_flag == -1
							|| !(request
									&& (request->flags
											& (1 << disable_callee_publish_flag)))))) {
		if(use_pubruri_avps)
			lock_get(&dlginfo->lock);
		dialog_publish_multi("Trying", dlginfo->pubruris_callee,
				&identity_remote, &identity_local, &(dlg->callid), 0,
				dlginfo->lifetime, 0, 0, 0, 0,
				(send_publish_flag == -1) ? 1 : 0, &(dlginfo->uuid));
		if(use_pubruri_avps)
			lock_release(&dlginfo->lock);
	}
}

static void __dialog_loaded(
		struct dlg_cell *dlg, int type, struct dlg_cb_params *_params)
{
	struct dlginfo_cell *dlginfo;

	LM_DBG("INVITE dialog loaded: from=%.*s\n", dlg->from_uri.len,
			dlg->from_uri.s);

	dlginfo = get_dialog_data(dlg, type, 0, 0);
	if(dlginfo != NULL) {
		LM_DBG("dialog info initialized (from=%.*s)\n", dlg->from_uri.len,
				dlg->from_uri.s);
		/* free_dlginfo_cell(dlginfo); */
	}
}


/**
 * init module function
 */
static int mod_init(void)
{
	bind_pua_t bind_pua;

	str s;
	pv_spec_t avp_spec;
	struct sip_uri ruri_uri;

	if(sruid_init(&_puadi_sruid, (char)'-', "padi", SRUID_INC) < 0) {
		return -1;
	}

	if(caller_dlg_var.len <= 0) {
		LM_WARN("pubruri_caller_dlg_var is not set"
				" - restore on restart disabled\n");
	}

	if(callee_dlg_var.len <= 0) {
		LM_WARN("pubruri_callee_dlg_var is not set"
				" - restore on restart disabled\n");
	}

	if((caller_entity_when_publish_disabled.len > 0)
			&& (disable_caller_publish_flag == -1)) {
		LM_WARN("caller_entity_when_publish_disabled is set but "
				"disable_caller_publish_flag is not defined"
				" - caller_entity_when_publish_disabled cannot be used \n");
	}

	if((callee_entity_when_publish_disabled.len > 0)
			&& (disable_callee_publish_flag == -1)) {
		LM_WARN("callee_entity_when_publish_disabled is set but "
				"disable_callee_publish_flag is not defined"
				" - callee_entity_when_publish_disabled cannot be used \n");
	}

	if((caller_entity_when_publish_disabled.len > 0)
			&& (parse_uri(caller_entity_when_publish_disabled.s,
						caller_entity_when_publish_disabled.len, &ruri_uri)
					< 0)) {
		LM_ERR("caller_entity_when_publish_disabled is not a valid SIP uri\n");
		return -1;
	}

	if((callee_entity_when_publish_disabled.len > 0)
			&& (parse_uri(callee_entity_when_publish_disabled.s,
						callee_entity_when_publish_disabled.len, &ruri_uri)
					< 0)) {
		LM_ERR("callee_entity_when_publish_disabled is not a valid SIP uri\n");
		return -1;
	}

	bind_pua = (bind_pua_t)find_export("bind_pua", 1, 0);
	if(!bind_pua) {
		LM_ERR("Can't bind pua\n");
		return -1;
	}

	if(bind_pua(&_pua_api) < 0) {
		LM_ERR("Can't bind pua\n");
		return -1;
	}
	if(_pua_api.send_publish == NULL) {
		LM_ERR("Could not import send_publish\n");
		return -1;
	}
	pua_send_publish = _pua_api.send_publish;

	/* bind to the dialog API */
	if(load_dlg_api(&dlg_api) != 0) {
		LM_ERR("failed to find dialog API - is dialog module loaded?\n");
		return -1;
	}
	/* register dialog creation callback */
	if(dlg_api.register_dlgcb(NULL, DLGCB_CREATED, __dialog_created, NULL, NULL)
			!= 0) {
		LM_ERR("cannot register callback for dialog creation\n");
		return -1;
	}
	/* register dialog loaded callback */
	if(dlg_api.register_dlgcb(NULL, DLGCB_LOADED, __dialog_loaded, NULL, NULL)
			!= 0) {
		LM_ERR("cannot register callback for dialog loaded\n");
		return -1;
	}

	if(use_pubruri_avps) {
		LM_DBG("configured to use avps for uri values\n");
		if((pubruri_caller_avp == NULL || *pubruri_caller_avp == 0)
				|| (pubruri_callee_avp == NULL || *pubruri_callee_avp == 0)) {
			LM_ERR("pubruri_caller_avp and pubruri_callee_avp must be set,"
				   " if use_pubruri_avps is enabled\n");
			return -1;
		}

		s.s = pubruri_caller_avp;
		s.len = strlen(s.s);
		if(pv_parse_spec(&s, &avp_spec) == 0 || avp_spec.type != PVT_AVP) {
			LM_ERR("malformed or non AVP %s AVP definition\n",
					pubruri_caller_avp);
			return -1;
		}
		if(pv_get_avp_name(0, &avp_spec.pvp, &pubruri_caller_avp_name,
				   &pubruri_caller_avp_type)
				!= 0) {
			LM_ERR("[%s]- invalid AVP definition\n", pubruri_caller_avp);
			return -1;
		}

		s.s = pubruri_callee_avp;
		s.len = strlen(s.s);
		if(pv_parse_spec(&s, &avp_spec) == 0 || avp_spec.type != PVT_AVP) {
			LM_ERR("malformed or non AVP %s AVP definition\n",
					pubruri_callee_avp);
			return -1;
		}
		if(pv_get_avp_name(0, &avp_spec.pvp, &pubruri_callee_avp_name,
				   &pubruri_callee_avp_type)
				!= 0) {
			LM_ERR("[%s]- invalid AVP definition\n", pubruri_callee_avp);
			return -1;
		}
	} else {
		LM_DBG("configured to use headers for uri values\n");
	}

	if(publish_dialog_req_within)
		dialog_event_types |= DLGCB_REQ_WITHIN;

	return 0;
}

/**
 * @brief Initialize module children
 */
static int child_init(int rank)
{
	if(sruid_init(&_puadi_sruid, (char)'-', "padi", SRUID_INC) < 0) {
		return -1;
	}

	if(rank != PROC_MAIN) {
		return 0;
	}

	return 0;
}

void free_dlginfo_cell(void *param)
{

	struct dlginfo_cell *cell = NULL;

	if(param == NULL)
		return;

	cell = param;
	free_str_list_all(cell->pubruris_caller);
	free_str_list_all(cell->pubruris_callee);

	if(use_pubruri_avps)
		lock_destroy(cell->lock);
	shm_free(param);
}


int is_ruri_in_list(struct str_list *list, str *ruri)
{
	struct str_list *pubruris = list;
	LM_DBG("search:'%.*s'\n", ruri->len, ruri->s);
	while(pubruris) {
		LM_DBG("compare with:'%.*s'\n", pubruris->s.len, pubruris->s.s);
		if(str_strcmp(&pubruris->s, ruri) == 0) {
			return 1;
		}
		pubruris = pubruris->next;
	}
	return 0;
}

void free_str_list_all(struct str_list *del_current)
{

	struct str_list *del_next;

	while(del_current) {

		del_next = del_current->next;
		if(del_current->s.s)
			shm_free(del_current->s.s);
		shm_free(del_current);
		del_current = del_next;
	}
}
