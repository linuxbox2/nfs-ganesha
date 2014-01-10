/*
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file  nfs_read_conf.c
 * @brief This file that contain the routine required for parsing the NFS specific configuraion file.
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>		/* for having FNDELAY */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include "log.h"
#include "ganesha_rpc.h"
#include "fsal.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_file_handle.h"
#include "nfs_exports.h"
#include "nfs_tools.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "config_parsing.h"

static struct config_item_list protocols[] = {
	CONFIG_LIST_TOK("3", CORE_OPTION_NFSV3),
	CONFIG_LIST_TOK("4", CORE_OPTION_NFSV4),
	CONFIG_LIST_EOL
};

static struct config_item core_params[] = {
	CONF_ITEM_UI16("NFS_Port", 1024, 65535, NFS_PORT,
		      nfs_core_param, port[P_NFS]),
/* 	CONF_ITEM_UI16("MNT_Port", 1024, 65535, 0, nfs_core_param, port[P_MNT]), */
/* 	CONF_ITEM_UI16("NLM_Port", 1024, 65535, 0, nfs_core_param, port[P_NLM]), */
	CONF_ITEM_UI16("Rquota_Port", 1024, 65535, RQUOTA_PORT,
		      nfs_core_param, port[P_RQUOTA]),
	CONF_ITEM_IPV4_ADDR("Bind_Addr", "0.0.0.0",
			    nfs_core_param, bind_addr),
	CONF_ITEM_UI32("NFS_Program", 1, 200499999, NFS_PROGRAM,
		       nfs_core_param, program[P_NFS]),
	CONF_ITEM_UI32("MNT_Program", 1, 200499999, MOUNTPROG,
				nfs_core_param, program[P_MNT]),
	CONF_ITEM_UI32("NLM_Program", 1, 200499999, NLMPROG,
		       nfs_core_param, program[P_NLM]),
	CONF_ITEM_UI32("Rquota_Program", 1, 200499999, RQUOTAPROG,
		       nfs_core_param, program[P_RQUOTA]),
	CONF_ITEM_UI32("Nb_Worker", 1, 4096, NB_WORKER_THREAD_DEFAULT,
		       nfs_core_param, nb_worker),
	CONF_ITEM_I64("Core_Dump_Size", -1, 1L<<36, -1,
		      nfs_core_param, core_dump_size),
	CONF_ITEM_BOOL("Drop_IO_Errors", true,
		       nfs_core_param, drop_io_errors),
	CONF_ITEM_BOOL("Drop_Inval_Errors", true,
		       nfs_core_param, drop_inval_errors),
	CONF_ITEM_BOOL("Drop_Delay_Errors", true,
		       nfs_core_param, drop_delay_errors),
	CONF_ITEM_UI32("Dispatch_Max_Reqs", 1, 10000, 5000,
		       nfs_core_param, dispatch_max_reqs),
	CONF_ITEM_UI32("Dispatch_Max_Reqs_Xprt", 1, 2048, 512,
		       nfs_core_param, dispatch_max_reqs_xprt),
	CONF_ITEM_BOOL("DRC_Disabled", false,
		       nfs_core_param, drc.disabled),
	CONF_ITEM_UI32("DRC_TCP_Npart", 1, 20, DRC_TCP_NPART,
		       nfs_core_param, drc.tcp.npart),
	CONF_ITEM_UI32("DRC_TCP_Size", 1, 32767, DRC_TCP_SIZE,
		       nfs_core_param, drc.tcp.size),
	CONF_ITEM_UI32("DRC_TCP_Cachesz", 1, 255, DRC_TCP_CACHESZ,
		       nfs_core_param, drc.tcp.cachesz),
	CONF_ITEM_UI32("DRC_TCP_Hiwat", 1, 256, DRC_TCP_HIWAT,
		       nfs_core_param, drc.tcp.hiwat),
	CONF_ITEM_UI32("DRC_TCP_Recycle_Npart", 1, 20, DRC_TCP_RECYCLE_NPART,
		       nfs_core_param, drc.tcp.recycle_npart),
	CONF_ITEM_UI32("DRC_TCP_Recycle_Expire_S", 0, 60*60, 600,
		       nfs_core_param, drc.tcp.recycle_expire_s),
	CONF_ITEM_BOOL("DRC_TCP_Checksum", DRC_TCP_CHECKSUM,
		       nfs_core_param, drc.tcp.checksum),
	CONF_ITEM_UI32("DRC_UDP_Npart", 1, 100, DRC_UDP_NPART,
		       nfs_core_param, drc.udp.npart),
	CONF_ITEM_UI32("DRC_UDP_Size", 512, 32768, DRC_UDP_SIZE,
		       nfs_core_param, drc.udp.size),
	CONF_ITEM_UI32("DRC_UDP_Cachesz", 1, 2047, DRC_UDP_CACHESZ,
		       nfs_core_param, drc.udp.cachesz),
	CONF_ITEM_UI32("DRC_UDP_Hiwat", 1, 32768, DRC_UDP_HIWAT,
		       nfs_core_param, drc.udp.hiwat),
	CONF_ITEM_BOOL("DRC_UDP_Checksum", DRC_UDP_CHECKSUM,
		       nfs_core_param, drc.udp.checksum),
	CONF_ITEM_UI32("RPC_Debug_Flags", 0, 0xFFFFFFFF, TIRPC_DEBUG_FLAGS,
		       nfs_core_param, rpc.debug_flags),
	CONF_ITEM_UI32("RPC_Max_Connections", 1, 10000, 1024,
		       nfs_core_param, rpc.max_connections),
	CONF_ITEM_UI32("RPC_Idle_Timeout_S", 0, 60*60, 300,
		       nfs_core_param, rpc.idle_timeout_s),
	CONF_ITEM_UI32("MaxRPCSendBufferSize", 1, 1048576*9,
		       NFS_DEFAULT_SEND_BUFFER_SIZE,
		       nfs_core_param, rpc.max_send_buffer_size),
	CONF_ITEM_UI32("MaxRPCRecvBufferSize", 1, 1048576*9,
		       NFS_DEFAULT_RECV_BUFFER_SIZE,
		       nfs_core_param, rpc.max_recv_buffer_size),
	CONF_ITEM_UI64("Long_Processing_Threshold", 1, 60, 10,
		       nfs_core_param, long_processing_threshold),
	CONF_ITEM_I64("Decoder_Fridge_Expiration_Delay", -1, 60*5, -1,
		      nfs_core_param, decoder_fridge_expiration_delay),
	CONF_ITEM_I64("Decoder_Fridge_Block_Timeout", -1, 60*5, -1,
		      nfs_core_param, decoder_fridge_block_timeout),
	CONF_ITEM_LIST("NFS_Protocols", CORE_OPTION_ALL_VERS, protocols,
		       nfs_core_param, core_options),
	CONF_ITEM_BOOL("NSM_Use_Caller_Name", false,
		       nfs_core_param, nsm_use_caller_name),
	CONF_ITEM_BOOL("Clustered", true,
		       nfs_core_param, clustered),
	CONF_ITEM_BOOL("Enable_NLM", true,
		       nfs_core_param, enable_NLM),
	CONF_ITEM_BOOL("Enable_RQUOTA", true,
		       nfs_core_param, enable_RQUOTA),
	CONFIG_EOL
};

struct config_block nfs_core = {
	.name = "NFS_Core_Param",
	.dbus_interface_name = "org.ganesha.nfsd.config.core",
	.params = core_params
};

/**
 * @brief Read the core configuration
 *
 * @param[in]  in_config Configuration file handle
 * @param[out] pparam    Read parameters
 *
 * @return 0 if ok, -1 if failed, 1 is stanza is not there.
 */
int nfs_read_core_conf(config_file_t in_config, nfs_core_parameter_t *pparam)
{
	int rc;

	rc = load_config_from_parse(in_config,
				    &nfs_core,
				    pparam,
				    true);
	return rc ? 1 : 0;
}

/**
 * @brief Reads the configuration for the IP/name.
 *
 * @param[in]  in_config Configuration file handle
 * @param[out] pparam    Read parameters
 *
 * @return 0 if ok,  -1 if not, 1 is stanza is not there.
 */
int nfs_read_ip_name_conf(config_file_t in_config,
			  nfs_ip_name_parameter_t *pparam)
{
	int var_max;
	int var_index;
	int err;
	char *key_name;
	char *key_value;
	config_item_t block;

	/* Get the config BLOCK */
	block = config_FindItemByName(in_config, CONF_LABEL_NFS_IP_NAME);

	if (block == NULL) {
		LogDebug(COMPONENT_CONFIG,
			 "Cannot read item \"%s\" from configuration file",
			 CONF_LABEL_NFS_IP_NAME);
		return 1;
	} else if (config_ItemType(block) != CONFIG_ITEM_BLOCK) {
		/* Expected to be a block */
		LogDebug(COMPONENT_CONFIG,
			 "Item \"%s\" is expected to be a block",
			 CONF_LABEL_NFS_IP_NAME);
		return 1;
	}

	var_max = config_GetNbItems(block);

	for (var_index = 0; var_index < var_max; var_index++) {
		config_item_t item;

		item = config_GetItemByIndex(block, var_index);

		/* Get key's name */
		err = config_GetKeyValue(item, &key_name, &key_value);

		if (err != 0) {
			LogCrit(COMPONENT_CONFIG,
				"Error reading key[%d] from section \"%s\" of configuration file.",
				var_index, CONF_LABEL_NFS_IP_NAME);
			return -1;
		}

		if (!strcasecmp(key_name, "Index_Size")) {
			pparam->hash_param.index_size = atoi(key_value);
		} else if (!strcasecmp(key_name, "Expiration_Time")) {
			pparam->expiration_time = atoi(key_value);
		} else if (!strcasecmp(key_name, "Map")) {
			pparam->mapfile = gsh_strdup(key_value);
			if (!pparam->mapfile) {
				LogFatal(COMPONENT_CONFIG,
					 "Unable to allocate memory for mapfile path.");
			}
		} else {
			LogCrit(COMPONENT_CONFIG,
				"Unknown or unsettable key: %s (item %s)",
				key_name, CONF_LABEL_NFS_IP_NAME);
			return -1;
		}
	}

	return 0;
}

#ifdef _HAVE_GSSAPI
/**
 *
 * @brief Read the configuration for krb5 stuff
 *
 * @param[in]  in_config Configuration file handle
 * @param[out] pparam    Read parameters
 *
 * @return 0 if ok, -1 if failed,1 is stanza is not there
 */
int nfs_read_krb5_conf(config_file_t in_config, nfs_krb5_parameter_t *pparam)
{
	int var_max;
	int var_index;
	int err;
	char *key_name;
	char *key_value;
	config_item_t block;

	/* Get the config BLOCK */
	block = config_FindItemByName(in_config, CONF_LABEL_NFS_KRB5);

	if (block == NULL) {
		LogDebug(COMPONENT_CONFIG,
			 "Cannot read item \"%s\" from configuration file",
			 CONF_LABEL_NFS_KRB5);
		return 1;
	} else if (config_ItemType(block) != CONFIG_ITEM_BLOCK) {
		/* Expected to be a block */
		LogDebug(COMPONENT_CONFIG,
			 "Item \"%s\" is expected to be a block",
			 CONF_LABEL_NFS_KRB5);
		return 1;
	}

	var_max = config_GetNbItems(block);

	for (var_index = 0; var_index < var_max; var_index++) {
		config_item_t item;

		item = config_GetItemByIndex(block, var_index);

		/* Get key's name */
		err = config_GetKeyValue(item, &key_name, &key_value);

		if (err != 0) {
			LogCrit(COMPONENT_CONFIG,
				"Error reading key[%d] from section \"%s\" of configuration file.",
				var_index, CONF_LABEL_NFS_KRB5);
			return -1;
		}

		if (!strcasecmp(key_name, "PrincipalName")) {
			if (strmaxcpy
			    (pparam->svc.principal, key_value,
			     sizeof(pparam->svc.principal)) == -1) {
				LogCrit(COMPONENT_CONFIG, "%s=\"%s\" too long",
					key_name, key_value);
			}
		} else if (!strcasecmp(key_name, "KeytabPath")) {
			if (strmaxcpy
			    (pparam->keytab, key_value,
			     sizeof(pparam->keytab)) == -1) {
				LogCrit(COMPONENT_CONFIG, "%s=\"%s\" too long",
					key_name, key_value);
			}
		} else if (!strcasecmp(key_name, "Active_krb5")) {
			pparam->active_krb5 = str_to_bool(key_value);
		} else {
			LogCrit(COMPONENT_CONFIG,
				"Unknown or unsettable key: %s (item %s)",
				key_name, CONF_LABEL_NFS_KRB5);
			return -1;
		}
	}

	return 0;
}
#endif

/**
 * @brief Read the configuration for NFSv4 stuff
 *
 * @param[in]  in_config Configuration file handle
 * @param[out] pparam    Read parameters
 *
 * @return 0 if ok, -1 if failed,1 is stanza is not there
 */
int nfs_read_version4_conf(config_file_t in_config,
			   nfs_version4_parameter_t *pparam)
{
	int var_max;
	int var_index;
	int err;
	char *key_name;
	char *key_value;
	config_item_t block;

	/* Get the config BLOCK */
	block = config_FindItemByName(in_config, CONF_LABEL_NFS_VERSION4);

	if (block == NULL) {
		LogDebug(COMPONENT_CONFIG,
			 "Cannot read item \"%s\" from configuration file",
			 CONF_LABEL_NFS_VERSION4);
		return 1;
	} else if (config_ItemType(block) != CONFIG_ITEM_BLOCK) {
		/* Expected to be a block */
		LogDebug(COMPONENT_CONFIG,
			 "Item \"%s\" is expected to be a block",
			 CONF_LABEL_NFS_VERSION4);
		return 1;
	}

	var_max = config_GetNbItems(block);

	for (var_index = 0; var_index < var_max; var_index++) {
		config_item_t item;

		item = config_GetItemByIndex(block, var_index);

		/* Get key's name */
		err = config_GetKeyValue(item, &key_name, &key_value);

		if (err != 0) {
			LogCrit(COMPONENT_CONFIG,
				"Error reading key[%d] from section \"%s\" of configuration file.",
				var_index, CONF_LABEL_NFS_VERSION4);
			return -1;
		}

		if (!strcasecmp(key_name, "Graceless")) {
			pparam->graceless = str_to_bool(key_value);
		} else if (!strcasecmp(key_name, "Lease_Lifetime")) {
			pparam->lease_lifetime = atoi(key_value);
		} else if (!strcasecmp(key_name, "Grace_Period")) {
			pparam->grace_period = atoi(key_value);
		} else if (!strcasecmp(key_name, "DomainName")) {
			pparam->domainname = gsh_strdup(pparam->domainname);
			if (!pparam->domainname) {
				LogFatal(COMPONENT_CONFIG,
					 "Unable to allocate memory for domain name.");
			}
		} else if (!strcasecmp(key_name, "IdmapConf")) {
			pparam->idmapconf = gsh_strdup(key_value);

			if (!pparam->idmapconf) {
				LogFatal(COMPONENT_CONFIG,
					 "Unable to allocate space for idmap conffile path.");
			}
		} else if (!strcasecmp(key_name, "UseGetpwnam")) {
			pparam->use_getpwnam = str_to_bool(key_value);
		} else if (!strcasecmp(key_name, "Allow_Numeric_Owners")) {
			pparam->allow_numeric_owners = str_to_bool(key_value);
		} else {
			LogWarn(COMPONENT_CONFIG,
				"Unknown or unsettable key: %s (item %s)",
				key_name, CONF_LABEL_NFS_VERSION4);
		}
	}

	return 0;
}				/* nfs_read_version4_conf */
