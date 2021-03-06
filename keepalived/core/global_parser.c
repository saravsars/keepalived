/*
 * Soft:        Keepalived is a failover program for the LVS project
 *              <www.linuxvirtualserver.org>. It monitor & manipulate
 *              a loadbalanced server pool using multi-layer checks.
 *
 * Part:        Configuration file parser/reader. Place into the dynamic
 *              data structure representation the conf file representing
 *              the loadbalanced server pool.
 *
 * Author:      Alexandre Cassen, <acassen@linux-vs.org>
 *
 *              This program is distributed in the hope that it will be useful,
 *              but WITHOUT ANY WARRANTY; without even the implied warranty of
 *              MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *              See the GNU General Public License for more details.
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Copyright (C) 2001-2012 Alexandre Cassen, <acassen@gmail.com>
 */

#include "config.h"

#include <netdb.h>
#include <stdlib.h>
#include "global_parser.h"
#include "global_data.h"
#include "check_data.h"
#include "parser.h"
#include "memory.h"
#include "smtp.h"
#include "utils.h"
#include "logger.h"

#define LVS_MAX_TIMEOUT		(86400*31)	/* 31 days */

/* data handlers */
/* Global def handlers */
static void
use_polling_handler(vector_t *strvec)
{
	global_data->linkbeat_use_polling = 1;
}
static void
routerid_handler(vector_t *strvec)
{
	FREE_PTR(global_data->router_id);
	global_data->router_id = set_value(strvec);
}
static void
emailfrom_handler(vector_t *strvec)
{
	FREE_PTR(global_data->email_from);
	global_data->email_from = set_value(strvec);
}
static void
smtpto_handler(vector_t *strvec)
{
	global_data->smtp_connection_to = atoi(vector_slot(strvec, 1)) * TIMER_HZ;
}
static void
smtpserver_handler(vector_t *strvec)
{
	int ret = -1;
	char *port_str = SMTP_PORT_STR;

	/* Has a port number been specified? */
	if (vector_size(strvec) >= 3)
		port_str = vector_slot(strvec,2);

	/* It can't be an IP address if it contains '-' or '/', and 
	   inet_stosockaddr() modifies the string if it contains either of them */
	if (!strpbrk(vector_slot(strvec, 1), "-/"))
		ret = inet_stosockaddr(vector_slot(strvec, 1), port_str, &global_data->smtp_server);

	if (ret < 0)
		domain_stosockaddr(vector_slot(strvec, 1), port_str, &global_data->smtp_server);
}
static void
smtphelo_handler(vector_t *strvec)
{
	char *helo_name;

	if (vector_size(strvec) < 2)
		return;

	helo_name = MALLOC(strlen(vector_slot(strvec, 1)) + 1);
	if (!helo_name)
		return;

	strcpy(helo_name, vector_slot(strvec, 1));
	global_data->smtp_helo_name = helo_name;
}
static void
email_handler(vector_t *strvec)
{
	vector_t *email_vec = read_value_block(strvec);
	unsigned int i;
	char *str;

	for (i = 0; i < vector_size(email_vec); i++) {
		str = vector_slot(email_vec, i);
		alloc_email(str);
	}

	free_strvec(email_vec);
}
#ifdef _WITH_LVS_
static void
lvs_timeouts(vector_t *strvec)
{
	int val;
	int i;
	char *endptr;

	if (vector_size(strvec) < 3) {
		log_message(LOG_INFO, "lvs_timeouts requires at least one option");
		return;
	}

	for (i = 1; i < vector_size(strvec); i++) {
		if (!strcmp(vector_slot(strvec, i), "tcp")) {
			if (i == vector_size(strvec) - 1) {
				log_message(LOG_INFO, "No value specified for lvs_timout tcp - ignoring");
				continue;
			}
			val = strtol(vector_slot(strvec, i+1), &endptr, 10);
			if (*endptr != '\0' || val < 0 || val > LVS_MAX_TIMEOUT)
				log_message(LOG_INFO, "Invalid lvs_timeout tcp (%s) - ignoring", FMT_STR_VSLOT(strvec, i+1));
			else
				global_data->lvs_tcp_timeout = val;
			i++;	/* skip over value */
			continue;
		}
		if (!strcmp(vector_slot(strvec, i), "tcpfin")) {
			if (i == vector_size(strvec) - 1) {
				log_message(LOG_INFO, "No value specified for lvs_timeout tcpfin - ignoring");
				continue;
			}
			val = strtol(vector_slot(strvec, i+1), &endptr, 10);
			if (*endptr != '\0' || val < 1 || val > LVS_MAX_TIMEOUT)
				log_message(LOG_INFO, "Invalid lvs_timeout tcpfin (%s) - ignoring", FMT_STR_VSLOT(strvec, i+1));
			else
				global_data->lvs_tcpfin_timeout = val;
			i++;	/* skip over value */
			continue;
		}
		if (!strcmp(vector_slot(strvec, i), "udp")) {
			if (i == vector_size(strvec) - 1) {
				log_message(LOG_INFO, "No value specified for lvs_timeout udp - ignoring");
				continue;
			}
			val = strtol(vector_slot(strvec, i+1), &endptr, 10);
			if (*endptr != '\0' || val < 1 || val > LVS_MAX_TIMEOUT)
				log_message(LOG_INFO, "Invalid lvs_timeout udp (%s) - ignoring", FMT_STR_VSLOT(strvec, i+1));
			else
				global_data->lvs_udp_timeout = val;
			i++;	/* skip over value */
			continue;
		}
		log_message(LOG_INFO, "Unknown option %s specified for lvs_timeouts", FMT_STR_VSLOT(strvec, i));
	}
}
#ifdef _WITH_LVS_
static void
lvs_syncd_handler(vector_t *strvec)
{
	int val;
	int i;
	char *endptr;

	if (global_data->lvs_syncd.ifname) {
		log_message(LOG_INFO, "lvs_sync_daemon has already been specified as %s %s - ignoring", global_data->lvs_syncd.ifname, global_data->lvs_syncd.vrrp_name);
		return;
	}

	if (vector_size(strvec) < 3) {
		log_message(LOG_INFO, "lvs_sync_daemon requires interface, VRRP instance");
		return;
	}

	global_data->lvs_syncd.ifname = set_value(strvec);

	global_data->lvs_syncd.vrrp_name = MALLOC(strlen(vector_slot(strvec, 2)) + 1);
	if (!global_data->lvs_syncd.vrrp_name)
		return;
	strcpy(global_data->lvs_syncd.vrrp_name, vector_slot(strvec, 2));

	/* This is maintained for backwards compatibility, prior to adding "id" option */
	if (vector_size(strvec) >= 4 && isdigit(FMT_STR_VSLOT(strvec, 3)[0])) {
		log_message(LOG_INFO, "Please use keyword \"id\" before lvs_sync_daemon syncid value");
		val = strtol(vector_slot(strvec,3), &endptr, 10);
		if (*endptr || val < 0 || val > 255)
			log_message(LOG_INFO, "Invalid syncid (%s) - defaulting to vrid", FMT_STR_VSLOT(strvec, 3));
		else
			global_data->lvs_syncd.syncid = val;
		i = 4;
	}
	else
		i = 3;

	for ( ; i < vector_size(strvec); i++) {
		if (!strcmp(vector_slot(strvec, i), "id")) {
			if (i == vector_size(strvec) - 1) {
				log_message(LOG_INFO, "No value specified for lvs_sync_daemon id, defaulting to vrid");
				continue;
			}
			val = strtol(vector_slot(strvec, i+1), &endptr, 10);
			if (*endptr != '\0' || val < 0 || val > 255)
				log_message(LOG_INFO, "Invalid syncid (%s) - defaulting to vrid", FMT_STR_VSLOT(strvec, i+1));
			else
				global_data->lvs_syncd.syncid = val;
			i++;	/* skip over value */
			continue;
		}
#ifdef _HAVE_IPVS_SYNCD_ATTRIBUTES_
		if (!strcmp(vector_slot(strvec, i), "maxlen")) {
			if (i == vector_size(strvec) - 1) {
				log_message(LOG_INFO, "No value specified for lvs_sync_daemon maxlen - ignoring");
				continue;
			}
			val = strtol(vector_slot(strvec, i+1), &endptr, 10);
			if (*endptr != '\0' || val < 1 || val > 65535 - 20 - 8)
				log_message(LOG_INFO, "Invalid lvs_sync_daemon maxlen (%s) - ignoring", FMT_STR_VSLOT(strvec, i+1));
			else
				global_data->lvs_syncd.sync_maxlen = val;
			i++;	/* skip over value */
			continue;
		}
		if (!strcmp(vector_slot(strvec, i), "port")) {
			if (i == vector_size(strvec) - 1) {
				log_message(LOG_INFO, "No value specified for lvs_sync_daemon port - ignoring");
				continue;
			}
			val = strtol(vector_slot(strvec, i+1), &endptr, 10);
			if (*endptr != '\0' || val < 1 || val > 65535)
				log_message(LOG_INFO, "Invalid lvs_sync_daemon port (%s) - ignoring", FMT_STR_VSLOT(strvec, i+1));
			else
				global_data->lvs_syncd.mcast_port = val;
			i++;	/* skip over value */
			continue;
		}
		if (!strcmp(vector_slot(strvec, i), "ttl")) {
			if (i == vector_size(strvec) - 1) {
				log_message(LOG_INFO, "No value specified for lvs_sync_daemon ttl - ignoring");
				continue;
			}
			val = strtol(vector_slot(strvec, i+1), &endptr, 10);
			if (*endptr != '\0' || val < 1 || val > 255)
				log_message(LOG_INFO, "Invalid lvs_sync_daemon ttl (%s) - ignoring", FMT_STR_VSLOT(strvec, i+1));
			else
				global_data->lvs_syncd.mcast_ttl = val;
			i++;	/* skip over value */
			continue;
		}
		if (!strcmp(vector_slot(strvec, i), "group")) {
			if (i == vector_size(strvec) - 1) {
				log_message(LOG_INFO, "No value specified for lvs_sync_daemon group - ignoring");
				continue;
			}

			if (inet_stosockaddr(vector_slot(strvec, i+1), NULL, &global_data->lvs_syncd.mcast_group) < 0)
				log_message(LOG_INFO, "Invalid lvs_sync_daemon group (%s) - ignoring", FMT_STR_VSLOT(strvec, i+1));

			if ((global_data->lvs_syncd.mcast_group.ss_family == AF_INET  && !IN_MULTICAST(htonl(((struct sockaddr_in *)&global_data->lvs_syncd.mcast_group)->sin_addr.s_addr))) ||
			    (global_data->lvs_syncd.mcast_group.ss_family == AF_INET6 && !IN6_IS_ADDR_MULTICAST(&((struct sockaddr_in6 *)&global_data->lvs_syncd.mcast_group)->sin6_addr))) {
				log_message(LOG_INFO, "lvs_sync_daemon group address %s is not multicast - ignoring", FMT_STR_VSLOT(strvec, i+1));
				global_data->lvs_syncd.mcast_group.ss_family = AF_UNSPEC;
			}

			i++;	/* skip over value */
			continue;
		}
#endif
		log_message(LOG_INFO, "Unknown option %s specified for lvs_sync_daemon", FMT_STR_VSLOT(strvec, i));
	}
}
#endif
static void
lvs_flush_handler(vector_t *strvec)
{
	global_data->lvs_flush = true;
}
#endif
#ifdef _WITH_VRRP_
static void
vrrp_mcast_group4_handler(vector_t *strvec)
{
	struct sockaddr_storage *mcast = &global_data->vrrp_mcast_group4;
	int ret;

	ret = inet_stosockaddr(vector_slot(strvec, 1), 0, mcast);
	if (ret < 0) {
		log_message(LOG_ERR, "Configuration error: Cant parse vrrp_mcast_group4 [%s]. Skipping"
				   , FMT_STR_VSLOT(strvec, 1));
	}
}
static void
vrrp_mcast_group6_handler(vector_t *strvec)
{
	struct sockaddr_storage *mcast = &global_data->vrrp_mcast_group6;
	int ret;

	ret = inet_stosockaddr(vector_slot(strvec, 1), 0, mcast);
	if (ret < 0) {
		log_message(LOG_ERR, "Configuration error: Cant parse vrrp_mcast_group6 [%s]. Skipping"
				   , FMT_STR_VSLOT(strvec, 1));
	}
}
static void
vrrp_garp_delay_handler(vector_t *strvec)
{
	global_data->vrrp_garp_delay = atoi(vector_slot(strvec, 1)) * TIMER_HZ;
}
static void
vrrp_garp_rep_handler(vector_t *strvec)
{
	global_data->vrrp_garp_rep = atoi(vector_slot(strvec, 1));
	if (global_data->vrrp_garp_rep < 1)
		global_data->vrrp_garp_rep = 1;
}
static void
vrrp_garp_refresh_handler(vector_t *strvec)
{
	global_data->vrrp_garp_refresh.tv_sec = atoi(vector_slot(strvec, 1));
}
static void
vrrp_garp_refresh_rep_handler(vector_t *strvec)
{
	global_data->vrrp_garp_refresh_rep = atoi(vector_slot(strvec, 1));
	if (global_data->vrrp_garp_refresh_rep < 1)
		global_data->vrrp_garp_refresh_rep = 1;
}
static void
vrrp_garp_lower_prio_delay_handler(vector_t *strvec)
{
	global_data->vrrp_garp_lower_prio_delay = atoi(vector_slot(strvec, 1)) * TIMER_HZ;
}
static void
vrrp_garp_lower_prio_rep_handler(vector_t *strvec)
{
	global_data->vrrp_garp_lower_prio_rep = atoi(vector_slot(strvec, 1));
	/* Allow 0 GARP messages to be sent */
	if (global_data->vrrp_garp_lower_prio_rep < 0)
		global_data->vrrp_garp_lower_prio_rep = 0;
}
static void
vrrp_garp_interval_handler(vector_t *strvec)
{
	global_data->vrrp_garp_interval = atof(vector_slot(strvec, 1)) * 1000000;
	if (global_data->vrrp_garp_interval >= 1000000)
		log_message(LOG_INFO, "The vrrp_garp_interval is very large - %s seconds", FMT_STR_VSLOT(strvec, 1));
}
static void
vrrp_gna_interval_handler(vector_t *strvec)
{
	global_data->vrrp_gna_interval = atof(vector_slot(strvec, 1)) * 1000000;
	if (global_data->vrrp_gna_interval >= 1000000)
		log_message(LOG_INFO, "The vrrp_gna_interval is very large - %s seconds", FMT_STR_VSLOT(strvec, 1));
}
static void
vrrp_lower_prio_no_advert_handler(vector_t *strvec)
{
	int res;

	if (vector_size(strvec) >= 2) {
		res = check_true_false(vector_slot(strvec,1));
		if (res < 0)
			log_message(LOG_INFO, "Invalid value for vrrp_lower_prio_no_advert specified");
		else
			global_data->vrrp_lower_prio_no_advert = res;
	}
	else
		global_data->vrrp_lower_prio_no_advert = true;
}
static void
vrrp_iptables_handler(vector_t *strvec)
{
	global_data->vrrp_iptables_inchain[0] = '\0';
	global_data->vrrp_iptables_outchain[0] = '\0';
	if (vector_size(strvec) >= 2) {
		if (strlen(vector_slot(strvec,1)) >= sizeof(global_data->vrrp_iptables_inchain)-1) {
			log_message(LOG_INFO, "VRRP Error : iptables in chain name too long - ignored");
			return;
		}
		strcpy(global_data->vrrp_iptables_inchain, vector_slot(strvec,1));
	}
	if (vector_size(strvec) >= 3) {
		if (strlen(vector_slot(strvec,2)) >= sizeof(global_data->vrrp_iptables_outchain)-1) {
			log_message(LOG_INFO, "VRRP Error : iptables out chain name too long - ignored");
			return;
		}
		strcpy(global_data->vrrp_iptables_outchain, vector_slot(strvec,2));
	}
}
#ifdef _HAVE_LIBIPSET_
static void
vrrp_ipsets_handler(vector_t *strvec)
{
	size_t len;

	if (vector_size(strvec) >= 2) {
		if (strlen(vector_slot(strvec,1)) >= sizeof(global_data->vrrp_ipset_address)-1) {
			log_message(LOG_INFO, "VRRP Error : ipset address name too long - ignored");
			return;
		}
		strcpy(global_data->vrrp_ipset_address, vector_slot(strvec,1));
	}
	else {
		global_data->using_ipsets = false;
		return;
	}

	if (vector_size(strvec) >= 3) {
		if (strlen(vector_slot(strvec,2)) >= sizeof(global_data->vrrp_ipset_address6)-1) {
			log_message(LOG_INFO, "VRRP Error : ipset IPv6 address name too long - ignored");
			return;
		}
		strcpy(global_data->vrrp_ipset_address6, vector_slot(strvec,2));
	}
	else {
		/* No second set specified, copy first name and add "6" */
		strcpy(global_data->vrrp_ipset_address6, global_data->vrrp_ipset_address);
		global_data->vrrp_ipset_address6[sizeof(global_data->vrrp_ipset_address6) - 2] = '\0';
		strcat(global_data->vrrp_ipset_address6, "6");
	}
	if (vector_size(strvec) >= 4) {
		if (strlen(vector_slot(strvec,3)) >= sizeof(global_data->vrrp_ipset_address_iface6)-1) {
			log_message(LOG_INFO, "VRRP Error : ipset IPv6 address_iface name too long - ignored");
			return;
		}
		strcpy(global_data->vrrp_ipset_address_iface6, vector_slot(strvec,3));
	}
	else {
		/* No third set specified, copy second name and add "_if6" */
		strcpy(global_data->vrrp_ipset_address_iface6, global_data->vrrp_ipset_address6);
		len = strlen(global_data->vrrp_ipset_address_iface6);
		if (global_data->vrrp_ipset_address_iface6[len-1] == '6')
			global_data->vrrp_ipset_address_iface6[--len] = '\0';
		global_data->vrrp_ipset_address_iface6[sizeof(global_data->vrrp_ipset_address_iface6) - 5] = '\0';
		strcat(global_data->vrrp_ipset_address_iface6, "_if6");
	}
}
#endif
static void
vrrp_version_handler(vector_t *strvec)
{
	uint8_t version = atoi(vector_slot(strvec, 1));
	if (VRRP_IS_BAD_VERSION(version)) {
		log_message(LOG_INFO, "VRRP Error : Version not valid !");
		log_message(LOG_INFO, "             must be between either 2 or 3. reconfigure !");
		return;
	}
	global_data->vrrp_version = version;
}
static void
vrrp_check_unicast_src_handler(vector_t *strvec)
{
	global_data->vrrp_check_unicast_src = 1;
}
static void
vrrp_check_adv_addr_handler(vector_t *strvec)
{
	global_data->vrrp_skip_check_adv_addr = 1;
}
static void
vrrp_strict_handler(vector_t *strvec)
{
	global_data->vrrp_strict = 1;
}
static void
vrrp_prio_handler(vector_t *strvec)
{
	int priority;

	if (vector_size(strvec) < 2) {
		log_message(LOG_INFO, "No vrrp process priority specified");
		return;
	}

	priority = atoi(vector_slot(strvec, 1));
	if (priority < -20 || priority > 19) {
		log_message(LOG_INFO, "Invalid vrrp process priority specified");
		return;
	}

	global_data->vrrp_process_priority = priority;
}
static void
vrrp_no_swap_handler(vector_t *strvec)
{
	global_data->vrrp_no_swap = true;
}
#endif
#ifdef _WITH_LVS_
static void
checker_prio_handler(vector_t *strvec)
{
	int priority;

	if (vector_size(strvec) < 2) {
		log_message(LOG_INFO, "No checker process priority specified");
		return;
	}

	priority = atoi(vector_slot(strvec, 1));
	if (priority < -20 || priority > 19) {
		log_message(LOG_INFO, "Invalid checker process priority specified");
		return;
	}

	global_data->checker_process_priority = priority;
}
static void
checker_no_swap_handler(vector_t *strvec)
{
	global_data->checker_no_swap = true;
}
#endif
#ifdef _WITH_SNMP_
static void
snmp_socket_handler(vector_t *strvec)
{
	if (vector_size(strvec) > 2) {
		log_message(LOG_INFO, "Too many parameters specified for snmp_socket - ignoring");
		return;
	}

	if (vector_size(strvec) < 2) {
		log_message(LOG_INFO, "SNMP error : snmp socket name missing");
		return;
	}

	if (strlen(vector_slot(strvec,1)) > PATH_MAX - 1) {
		log_message(LOG_INFO, "SNMP error : snmp socket name too long - ignored");
		return;
	}

	if (global_data->snmp_socket) {
		log_message(LOG_INFO, "SNMP socket already set to %s - ignoring", global_data->snmp_socket);
		return;
	}

	global_data->snmp_socket = MALLOC(strlen(vector_slot(strvec, 1) + 1));
	strcpy(global_data->snmp_socket, vector_slot(strvec,1));
}
static void
trap_handler(vector_t *strvec)
{
	global_data->enable_traps = true;
}
#ifdef _WITH_SNMP_KEEPALIVED_
static void
snmp_keepalived_handler(vector_t *strvec)
{
	global_data->enable_snmp_keepalived = true;
}
#endif
#ifdef _WITH_SNMP_RFC_
static void
snmp_rfc_handler(vector_t *strvec)
{
	global_data->enable_snmp_rfcv2 = true;
	global_data->enable_snmp_rfcv3 = true;
}
#endif
#ifdef _WITH_SNMP_RFCV2_
static void
snmp_rfcv2_handler(vector_t *strvec)
{
	global_data->enable_snmp_rfcv2 = true;
}
#endif
#ifdef _WITH_SNMP_RFCV3_
static void
snmp_rfcv3_handler(vector_t *strvec)
{
	global_data->enable_snmp_rfcv3 = true;
}
#endif
#ifdef _WITH_SNMP_CHECKER_
static void
snmp_checker_handler(vector_t *strvec)
{
	global_data->enable_snmp_checker = true;
}
#endif
#endif

void
global_init_keywords(void)
{
	/* global definitions mapping */
	install_keyword_root("linkbeat_use_polling", use_polling_handler, true);
	install_keyword_root("global_defs", NULL, true);
	install_keyword("router_id", &routerid_handler);
	install_keyword("notification_email_from", &emailfrom_handler);
	install_keyword("smtp_server", &smtpserver_handler);
	install_keyword("smtp_helo_name", &smtphelo_handler);
	install_keyword("smtp_connect_timeout", &smtpto_handler);
	install_keyword("notification_email", &email_handler);
#ifdef _WITH_LVS_
	install_keyword("lvs_timeouts", &lvs_timeouts);
	install_keyword("lvs_flush", &lvs_flush_handler);
#ifdef _WITH_LVS_
	install_keyword("lvs_sync_daemon", &lvs_syncd_handler);
#endif
#endif
#ifdef _WITH_VRRP_
	install_keyword("vrrp_mcast_group4", &vrrp_mcast_group4_handler);
	install_keyword("vrrp_mcast_group6", &vrrp_mcast_group6_handler);
	install_keyword("vrrp_garp_master_delay", &vrrp_garp_delay_handler);
	install_keyword("vrrp_garp_master_repeat", &vrrp_garp_rep_handler);
	install_keyword("vrrp_garp_master_refresh", &vrrp_garp_refresh_handler);
	install_keyword("vrrp_garp_master_refresh_repeat", &vrrp_garp_refresh_rep_handler);
	install_keyword("vrrp_garp_lower_prio_delay", &vrrp_garp_lower_prio_delay_handler);
	install_keyword("vrrp_garp_lower_prio_repeat", &vrrp_garp_lower_prio_rep_handler);
	install_keyword("vrrp_garp_interval", &vrrp_garp_interval_handler);
	install_keyword("vrrp_gna_interval", &vrrp_gna_interval_handler);
	install_keyword("vrrp_lower_prio_no_advert", &vrrp_lower_prio_no_advert_handler);
	install_keyword("vrrp_version", &vrrp_version_handler);
	install_keyword("vrrp_iptables", &vrrp_iptables_handler);
#ifdef _HAVE_LIBIPSET_
	install_keyword("vrrp_ipsets", &vrrp_ipsets_handler);
#endif
	install_keyword("vrrp_check_unicast_src", &vrrp_check_unicast_src_handler);
	install_keyword("vrrp_skip_check_adv_addr", &vrrp_check_adv_addr_handler);
	install_keyword("vrrp_strict", &vrrp_strict_handler);
	install_keyword("vrrp_priority", &vrrp_prio_handler);
	install_keyword("vrrp_no_swap", &vrrp_no_swap_handler);
#endif
#ifdef _WITH_LVS_
	install_keyword("checker_priority", &checker_prio_handler);
	install_keyword("checker_no_swap", &checker_no_swap_handler);
#endif
#ifdef _WITH_SNMP_
	install_keyword("snmp_socket", &snmp_socket_handler);
	install_keyword("enable_traps", &trap_handler);
#ifdef _WITH_SNMP_KEEPALIVED_
	install_keyword("enable_snmp_keepalived", &snmp_keepalived_handler);
#endif
#ifdef _WITH_SNMP_RFC_
	install_keyword("enable_snmp_rfc", &snmp_rfc_handler);
#endif
#ifdef _WITH_SNMP_RFCV2_
	install_keyword("enable_snmp_rfcv2", &snmp_rfcv2_handler);
#endif
#ifdef _WITH_SNMP_RFCV3_
	install_keyword("enable_snmp_rfcv3", &snmp_rfcv3_handler);
#endif
#ifdef _WITH_SNMP_CHECKER_
	install_keyword("enable_snmp_checker", &snmp_checker_handler);
#endif
#endif
}
