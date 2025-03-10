/*
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


#ifndef USRLOC_H
#define USRLOC_H

#include <time.h>
#include "../../core/counters.h"
#include "ul_callback.h"
#include "../../core/qvalue.h"
#include "../../core/str.h"
#include "../../modules/tm/dlg.h"
#include "../cdp/diameter_ims_code_avp.h"

#define NO_DB 0
#define WRITE_THROUGH 1
#define WRITE_BACK 2 //not implemented yet
#define DB_ONLY 3

#define SEARCH_NORMAL 0
#define SEARCH_RECEIVED 1

#define ALIAS "alias="
#define ALIAS_LEN (sizeof(ALIAS) - 1)

#define RINSTANCE "rinstance="
#define RINSTANCE_LEN (sizeof(RINSTANCE) - 1)

#define VALID_CONTACT(c, t) ((c->expires > t) || (c->expires == 0))

#define SEARCH_SERVICE_ROUTES \
	1 << 0 /* bitmap of extra search functionality when searching for contacts */

struct hslot; /*!< Hash table slot */
struct socket_info;

int get_alias_host_from_contact(str *contact_uri_params, str *alias_host);

struct udomain
{
	str *name;			 /*!< Domain name (NULL terminated) */
	int size;			 /*!< Hash table size */
	struct hslot *table; /*!< Hash table - array of collision slots */
	/* statistics */
	stat_var *contacts; /*!< no of registered contacts */
	stat_var *expired;	/*!< no of expires */
};
typedef struct udomain udomain_t;

/** Public Identity Structure */
typedef struct
{
	char barring;		 /**< Barring state									*/
	str public_identity; /**< Public Identity string							*/
	str wildcarded_psi;	 /** if exists is the wildcarded psi					*/
} ims_public_identity;

/** TLS SA Information */
typedef struct tls
{
	unsigned short port_tls; /**< Port UE TLS						*/
	unsigned long session_hash;
} tls_t;

/** IPSec SA Information */
typedef struct ipsec
{
	unsigned int spi_uc;	/**< SPI Client to use					*/
	unsigned int spi_us;	/**< SPI Server to use					*/
	unsigned int spi_pc;	/**< SPI Client to use					*/
	unsigned int spi_ps;	/**< SPI Server to use					*/
	unsigned short port_uc; /**< Port UE Client						*/
	unsigned short port_us; /**< Port UE Server						*/
	unsigned short port_pc; /**< Port Proxy Client		*/
	unsigned short port_ps; /**< Port Proxy Server      */

	str ealg;	/**< Cypher Algorithm - ESP				*/
	str r_ealg; /**< received Cypher Algorithm - ESP	*/
	str ck;		/**< Cypher Key							*/
	str alg;	/**< Integrity Algorithm - AH			*/
	str r_alg;	/**<received Integrity Algorithm - AH	*/
	str ik;		/**< Integrity Key						*/
	str prot;	/**< Protocol (ah/esp) */
	str mod;	/**< Mode (transport/tunnel) */
} ipsec_t;

typedef enum sec_type
{
	SECURITY_NONE = 0,
	SECURITY_IPSEC = 1,
	SECURITY_TLS = 2,
} security_type;

typedef struct security
{
	str sec_header;		/**< Security Header value 				*/
	security_type type; /**< Type of security in use			*/

	union
	{
		ipsec_t *ipsec; /**< IPSec SA information, if any		*/
		tls_t *tls;		/**< TLS SA information, if any		*/
	} data;
	float q;
} security_t;

typedef struct udomain_head
{
	str *name;
} udomain_head_t;

/* public identity structure. To be associated with a contact */
typedef struct ppublic
{
	str public_identity; /**< Public identity */
	char is_default;	 /**< is this the default identity for the contact */
	struct ppublic *next;
	struct ppublic *prev;
} ppublic_t;

/** Enumeration for public identity Registration States */
enum pcontact_reg_states
{
	PCONTACT_ANY = 0,
	PCONTACT_NOT_REGISTERED =
			1 << 0,				  /**< User not-registered, no profile stored	*/
	PCONTACT_REGISTERED = 1 << 1, /**< User registered						*/
	PCONTACT_REG_PENDING = 1 << 2, /**< User not-registered, profile stored	*/
	PCONTACT_REG_PENDING_AAR =
			1 << 3, /**< User not-registered, profile stored, AAR sent	*/
	PCONTACT_DEREGISTERED = 1 << 4,
	PCONTACT_DEREG_PENDING_PUBLISH = 1 << 5
};

static inline char *reg_state_to_string(enum pcontact_reg_states reg_state)
{
	switch(reg_state) {
		case PCONTACT_NOT_REGISTERED:
			return "not registered";
		case PCONTACT_REGISTERED:
			return "registered";
		case PCONTACT_REG_PENDING:
			return "registration pending";
		case PCONTACT_DEREGISTERED:
			return "unregistered";
		case PCONTACT_DEREG_PENDING_PUBLISH:
			return "deregistration pending, publish sent";
		case PCONTACT_REG_PENDING_AAR:
			return "registration pending, aar sent";
		default:
			return "unknown";
	}
}

typedef struct pcontact_info
{
	unsigned short searchflag; /*!search flag - what to search on */
	unsigned short extra_search_criteria;
	str aor; /* contact uri */
	str via_host;
	unsigned short via_port;
	unsigned short via_prot;
	str received_host;
	unsigned short received_port;
	unsigned short received_proto; /*!< from transport */
	str *path;
	time_t expires;
	str *callid;
	str *public_ids;
	int num_public_ids;
	str *service_routes;
	int num_service_routes;
	str *rx_regsession_id;
	enum pcontact_reg_states reg_state;
} pcontact_info_t;

/*! \brief
 * Basic hash table element
 */
typedef struct pcontact
{
	unsigned int aorhash; /*!< Hash over address of record */
	unsigned int sl;	  /*!< slot number */
	struct hslot
			*slot; /*!< Collision slot in the hash table array we belong to */
	str *domain; /*!< Pointer to domain we belong to (null terminated string) */
	str aor;	 /*!< Address of record */
	str rinstance;
	str contact_host;			 /*!< host part of contact */
	str contact_user;			 /*!< user part of contact */
	unsigned short contact_port; /*!< port part of contact */
	str callid;					 /*!< Call-ID */
	str received_host; /*!< host part of src address where register came from */
	unsigned short received_port; /*!< port register was received from */
	unsigned short received_proto;
	/*!< from transport */;
	str via_host;
	unsigned short via_port;
	unsigned short via_proto;
	str path;		   /*!< Path header */
	str rx_session_id; /*!< Rx Session ID for registration Rx AF session - not used if not using diameter_rx */
	enum pcontact_reg_states reg_state; /*!< Reg state of contact */
	time_t expires;						/*!< expires time for contact */
	str *service_routes;				/*!< Array of service routes */
	unsigned short num_service_routes;	/*!< Number of service routes */
	security_t *security;				/**< Security-Client Information		*/
	security_t *security_temp; /**< Security-Client Information (temp)	*/
	ppublic_t *head;		   /*!< list of associated public identities */
	ppublic_t *tail;
	struct socket_info *sock;  /*!< received socket */
	struct ulcb_head_list cbs; /*!< contact callback list */
	struct pcontact *prev;	   /*!< Next item in the hash entry */
	struct pcontact *next;	   /*!< Previous item in the hash entry */
} pcontact_t;

typedef int (*get_pcontact_t)(struct udomain *_d, pcontact_info_t *contact_info,
		struct pcontact **_c, int reverse_search);

typedef int (*db_load_pcontact_t)(udomain_t *_d, str *_aor, int insert_cache,
		struct pcontact **_c, pcontact_info_t *contact_info);
extern struct ul_callback *cbp_registrar;
extern struct ul_callback *cbp_qos;
typedef int (*assert_identity_t)(struct udomain *_d, str *_host,
		unsigned short _port, unsigned short _proto, str *_identity);

typedef int (*insert_pcontact_t)(struct udomain *_d, str *_aor,
		struct pcontact_info *ci, struct pcontact **_c);
typedef int (*delete_pcontact_t)(struct udomain *_d, struct pcontact *_c);
typedef int (*unreg_pending_contacts_cb_t)(
		struct udomain *_d, struct pcontact *_c, int type);
typedef int (*update_pcontact_t)(
		struct udomain *_d, struct pcontact_info *ci, struct pcontact *_c);
typedef int (*update_rx_regsession_t)(
		struct udomain *_d, str *session_id, struct pcontact *_c);

typedef void (*lock_udomain_t)(struct udomain *_d, str *via_host,
		unsigned short via_port, unsigned short via_proto);
typedef void (*unlock_udomain_t)(struct udomain *_d, str *via_host,
		unsigned short via_port, unsigned short via_proto);
typedef int (*register_udomain_t)(const char *_n, struct udomain **_d);
typedef int (*get_udomain_t)(const char *_n, udomain_t **_d);
typedef int (*get_all_ucontacts_t)(void *buf, int len, unsigned int flags,
		unsigned int part_idx, unsigned int part_max);

/*security related API signatures */
typedef int (*update_security_t)(struct udomain *_d, security_type _t,
		security_t *_s, struct pcontact *_c);
typedef int (*update_temp_security_t)(struct udomain *_d, security_type _t,
		security_t *_s, struct pcontact *_c);

/* statistic APIs */
typedef unsigned long (*get_number_of_contacts_t)();

/*! usrloc API export structure */
typedef struct usrloc_api
{
	int use_domain; /*! use_domain module parameter */
	int db_mode;	/*! db_mode module parameter */

	register_udomain_t register_udomain;
	get_udomain_t get_udomain;
	lock_udomain_t lock_udomain;
	unlock_udomain_t unlock_udomain;

	insert_pcontact_t insert_pcontact;
	delete_pcontact_t delete_pcontact;
	unreg_pending_contacts_cb_t unreg_pending_contacts_cb;
	get_pcontact_t get_pcontact;
	assert_identity_t assert_identity;

	update_pcontact_t update_pcontact;
	update_rx_regsession_t update_rx_regsession;
	get_all_ucontacts_t get_all_ucontacts;

	update_security_t update_security;
	update_temp_security_t update_temp_security;

	register_ulcb_t register_ulcb;

	get_number_of_contacts_t get_number_of_contacts;

	is_ulcb_registered_t is_ulcb_registered;
	register_ulcb_t register_ulcb_method;
	db_load_pcontact_t db_load_pcontact;
} usrloc_api_t;

/*! usrloc API export bind function */
typedef int (*bind_usrloc_t)(usrloc_api_t *api);

#endif
