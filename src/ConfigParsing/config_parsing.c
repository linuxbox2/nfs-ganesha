/* ----------------------------------------------------------------------------
 * Copyright CEA/DAM/DIF  (2007)
 * contributeur : Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
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
#include "config.h"
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#if HAVE_STRING_H
#include <string.h>
#endif
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include "config_parsing.h"
#include "analyse.h"
#include "abstract_mem.h"
#include "conf_yacc.h"
#include "log.h"
#include "fsal_convert.h"

struct parser_state parser_state;

/* config_ParseFile:
 * Reads the content of a configuration file and
 * stores it in a memory structure.
 */
config_file_t config_ParseFile(char *file_path)
{

	struct parser_state *st = &parser_state;
	int rc;

	memset(st, 0, sizeof(struct parser_state));
	rc = ganesha_yy_init_parser(file_path, st);
	if (rc) {
		return NULL;
	}
	rc = ganesha_yyparse(st);
	ganesha_yylex_destroy(st->scanner);

	/* converts pointer to pointer */
	return rc ? NULL : (config_file_t) st->root_node;
}

/* If config_ParseFile returns a NULL pointer,
 * config_GetErrorMsg returns a detailled message
 * to indicate the reason for this error.
 */
char *config_GetErrorMsg()
{

	return "Help! Help! We're all gonna die!!!";

}

/**
 * config_Print:
 * Print the content of the syntax tree
 * to a file.
 */
void config_Print(FILE * output, config_file_t config)
{
	print_parse_tree(output, (struct config_root *)config);
}

/** 
 * config_Free:
 * Free the memory structure that store the configuration.
 */

void config_Free(config_file_t config)
{
	free_parse_tree((struct config_root *)config);
	return;

}

static bool convert_bool(struct config_node *node,
			 bool *b)
{
	if (!strcasecmp(node->u.varvalue, "1") ||
	    !strcasecmp(node->u.varvalue, "yes") ||
	    !strcasecmp(node->u.varvalue, "true")) {
		*b = true;
		return true;
	}
	if (!strcasecmp(node->u.varvalue, "0") ||
	    !strcasecmp(node->u.varvalue, "no") ||
	    !strcasecmp(node->u.varvalue, "false")) {
		*b = false;
		return true;
	}
	LogMajor(COMPONENT_CONFIG,
		 "At (%s:%d): %s (%s) should be 'true' or 'false'",
		 node->filename,
		 node->linenumber,
		 node->name,
		 node->u.varvalue);
	return false;
}

static bool convert_int(struct config_node *node,
			int64_t min, int64_t max,
			int64_t *num)
{
	int64_t val;
	char *endptr;

	errno = 0;
	val = strtoll(node->u.varvalue, &endptr, 10);
	if (*node->u.varvalue != '\0' && *endptr == '\0') {
		if (errno != 0 || val < min || val > max) {
			LogMajor(COMPONENT_CONFIG,
				 "At (%s:%d): %s (%s) is out of range",
				 node->filename,
				 node->linenumber,
				 node->name,
				 node->u.varvalue);
			return false;
		}
	} else {
		LogMajor(COMPONENT_CONFIG,
			 "At (%s:%d): %s (%s) is not an integer",
			 node->filename,
			 node->linenumber,
			 node->name,
			 node->u.varvalue);
		return false;
	}
	*num = val;
	return true;
}

static bool convert_uint(struct config_node *node,
			 uint64_t min, uint64_t max,
			 uint64_t *num)
{
	uint64_t val;
	char *endptr;

	errno = 0;
	val = strtoull(node->u.varvalue, &endptr, 10);
	if (*node->u.varvalue != '\0' && *endptr == '\0') {
		if (errno != 0 || val < min || val > max) {
			LogMajor(COMPONENT_CONFIG,
				 "At (%s:%d): %s (%s) is out of range",
				 node->filename,
				 node->linenumber,
				 node->name,
				 node->u.varvalue);
			return false;
		}
	} else {
		LogMajor(COMPONENT_CONFIG,
			 "At (%s:%d): %s (%s) is not an integer",
			 node->filename,
			 node->linenumber,
			 node->name,
			 node->u.varvalue);
		return false;
	}
	*num = val;
	return true;
}

/* scan a list of CSV tokens.  move to parser!
 */

static int convert_list(struct config_node *node,
			struct config_item *item,
			uint32_t *flags)
{
	struct config_item_list *tok;
	char *csv_list = alloca(strlen(node->u.varvalue) + 1);
	char *sp, *cp, *ep;
	bool found;
	int errors = 0;

	*flags = 0;
	strcpy(csv_list, node->u.varvalue);
	sp = csv_list;
	ep = sp + strlen(sp);
	while (cp < ep) {
		cp = index(sp, ',');
		if (cp != NULL)
			*cp++ = '\0';
		else
			cp = ep;
		tok = item->u.lst.tokens;
		found = false;
		while (tok->token != NULL) {
			if (strcasecmp(sp, tok->token) == 0) {
				*flags |= tok->value;
				found = true;
			}
			tok++;
		}
		if (!found) {
			LogMajor(COMPONENT_CONFIG,
				 "At (%s:%d): %s has unknown token (%s)",
				 node->filename,
				 node->linenumber,
				 node->name,
				 sp);
			errors++;
		}
		sp = cp;
	}
	return errors;
}

static int convert_enum(struct config_node *node,
			struct config_item *item,
			uint32_t *val)
{
	struct config_item_list *tok;
	bool found;
	int errors = 0;

	tok = item->u.lst.tokens;
	found = false;
	while (tok->token != NULL) {
		if (strcasecmp(node->u.varvalue, tok->token) == 0) {
			*val = tok->value;
			found = true;
		}
		tok++;
	}
	if (!found) {
		LogMajor(COMPONENT_CONFIG,
			 "At (%s:%d): %s has unknown token (%s)",
			 node->filename,
			 node->linenumber,
			 node->name,
			 node->u.varvalue);
		errors++;
	}
	return errors;
}

static int convert_inet_addr(struct config_node *node,
			     struct config_item *item,
			     int ai_family,
			     struct sockaddr *sock)
{
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	int rc;

	hints.ai_flags = AI_ADDRCONFIG;
	hints.ai_family = ai_family;
	hints.ai_socktype = 0;
	hints.ai_protocol = 0;
	rc = getaddrinfo(node->u.varvalue, NULL,
			 &hints, &res);
	if (rc == 0) {
		memcpy(sock, res->ai_addr, res->ai_addrlen);
		if (res->ai_next != NULL)
			LogInfo(COMPONENT_CONFIG,
				"At (%s:%d): Multiple addresses for %s = %s",
				node->filename,
				node->linenumber,
				node->name,
				node->u.varvalue);
	} else {
		LogMajor(COMPONENT_CONFIG,
			 "At (%s:%d): No IP address found for %s = %s because:%s",
			 node->filename,
			 node->linenumber,
			 item->name, node->u.varvalue,
			 gai_strerror(rc));
	}
	freeaddrinfo(res);
	return rc;
}

static int do_block_init(struct config_item *params,
			 void *param_struct);

static int do_block_load(struct config_node *blk,
			 struct config_item *params,
			 void *param_struct);

/**
 * @brief Process a sub-block
 *
 * The item arg supplies two function pointers that
 * are defined as follows:
 *
 * param_mem
 *  This function manages memory for the sub-block's processing.
 *  It has two arguments, a pointer to the parent param struct and
 *  a pointer to the child param struct.
 *  If the child argument is NULL, it returns a pointer to a usable
 *  child param struct.  This can either be allocate memory or a pointer to
 *  existing memory.
 *
 *  If the child argument is not NULL, it is the pointer it returned above.
 *  The function reverts whatever it did above.
 *
 * attach
 *  This function attaches the build param struct to its parent.
 *  If the second argument is NULL, the function initializes
 *  the parent's linkage.  For glists, this would be a glist_init().
 *  It is called by both do_subblock for this block and by
 *  do_block_init() for the parent.  This makes the parent whole
 *  even if this block parse throws errors.
 *
 * Both of these functions are called in the context of the parent parse.
 * In particular, the attach initialization is to the parent, not the child.
 * The child does its own init of things like glist in param_mem.
 *
 * @param node - parse node of the subblock
 * @param item - config_item describing block
 * @param parent - pointer to the parent structure
 *
 * @ return 0 on success, non-zero on errors
 */

static int do_subblock(struct config_node *node,
		       struct config_item *item,
		       void *parent)
{
	void *param_struct;
	int rc = 0;

	if (node->type != TYPE_BLOCK) {
		LogCrit(COMPONENT_CONFIG,
			"At (%s:%d): %s is not a block!",
			 node->filename,
			 node->linenumber,
			item->name);
		return 1;
	}
	param_struct = item->u.blk.param_mem(parent, NULL);
	if (param_struct == NULL) {
		LogCrit(COMPONENT_CONFIG,
			"At (%s:%d): Could not allocate subblock for %s",
			 node->filename,
			 node->linenumber,
			item->name);
		return 1;
	}
	rc = do_block_init(item->u.blk.sub_blk, param_struct);
	if (rc != 0) {
		LogCrit(COMPONENT_CONFIG,
			"At (%s:%d): Could not initialize parameters for %s",
			node->filename,
			node->linenumber,
			item->name);
		item->u.blk.param_mem(parent, param_struct);
		return 1;
	}
	rc = do_block_load(node, item->u.blk.sub_blk, param_struct);
	if (rc != 0) {
		LogCrit(COMPONENT_CONFIG,
			"At (%s:%d): Could not process parameters for %s",
			node->filename,
			node->linenumber,
			item->name);
		item->u.blk.param_mem(parent, param_struct);
		return 1;
	}
	item->u.blk.attach(parent, param_struct);
	return 0;
}

/**
 * @brief Lookup the first node in the list by this name
 *
 * @param list - head of the glist
 * @param name - node name of interest
 *
 * @return first matching node or NULL
 */

static struct config_node *lookup_node(struct glist_head *list,
				       const char *name)
{
	struct config_node *node;
	struct glist_head *ns;
	
	glist_for_each(ns, list) {
		node = glist_entry(ns, struct config_node, node);
		if (strcasecmp(name, node->name) == 0) {
			node->found = true;
			return node;
		}
	}
	return NULL;
}

/**
 * @brief Lookup the next node in list
 *
 * @param list - head of the glist
 * @param start - continue the lookup from here
 * @param name - node name of interest
 *
 * @return first matching node or NULL
 */


static struct config_node *lookup_next_node(struct glist_head *list,
				     struct glist_head *start,
				     const char *name)
{
	struct config_node *node;
	struct glist_head *ns;

	glist_for_each_next(start, ns, list) {
		node = glist_entry(ns, struct config_node, node);
		if (strcasecmp(name, node->name) == 0) {
			node->found = true;
			return node;
		}
	}
	return NULL;
}

static int do_block_init(struct config_item *params,
			 void *param_struct)
{
	struct config_item *item;
	caddr_t *param_addr;
	struct sockaddr_in *sock;
	struct sockaddr_in6 *sock6;
	int rc;
	int errors = 0;

	for (item = params; item->name != NULL; item++) {
		param_addr = (caddr_t *)((uint64_t)param_struct + item->off);
		switch (item->type) {
		case CONFIG_INT16:
			*(int16_t *)param_addr = item->u.i16.def;
			break;
		case CONFIG_UINT16:
			*(uint16_t *)param_addr = item->u.ui16.def;
			break;
		case CONFIG_INT32:
			*(int32_t *)param_addr = item->u.i32.def;
			break;
		case CONFIG_UINT32:
			*(uint32_t *)param_addr = item->u.ui32.def;
			break;
		case CONFIG_INT64:
			*(int64_t *)param_addr = item->u.i64.def;
			break;
		case CONFIG_UINT64:
			*(uint64_t *)param_addr = item->u.ui64.def;
			break;
		case CONFIG_STRING:
		case CONFIG_PATH:
			if (item->u.str.def)
				*(char **)param_addr = gsh_strdup(item->u.str.def);
			else
				*(char **)param_addr = NULL;
			break;
		case CONFIG_BOOL:
			*(bool *)param_addr = item->u.b.def;
			break;
		case CONFIG_LIST:
		case CONFIG_ENUM:
			*(uint32_t *)param_addr = item->u.lst.def;
			break;
		case CONFIG_IPV4_ADDR:
			sock = (struct sockaddr_in *)param_addr;
			memset(&sock->sin_addr, 0, sizeof(struct in_addr));
			sock->sin_family = AF_INET;
			rc = inet_pton(AF_INET,
				       item->u.ipv4.def, &sock->sin_addr);
			if (rc <= 0) {
				LogWarn(COMPONENT_CONFIG,
					"Cannot set IPv4 default for %s to %s",
					item->name, item->u.ipv4.def);
				errors++;
			}
			break;
		case CONFIG_IPV6_ADDR:
			sock6 = (struct sockaddr_in6 *)param_addr;
			memset(&sock6->sin6_addr, 0, sizeof(struct in6_addr));
			sock6->sin6_family = AF_INET6;
			rc = inet_pton(AF_INET6,
				       item->u.ipv6.def, &sock6->sin6_addr);
			if (rc <= 0) {
				LogWarn(COMPONENT_CONFIG,
					"Cannot set IPv4 default for %s to %s",
					item->name, item->u.ipv6.def);
				errors++;
			}
			break;
		case CONFIG_INET_PORT:
			*(uint16_t *)param_addr = htons(item->u.ui16.def);
			break;
		case CONFIG_BLOCK:
			item->u.blk.attach(param_addr, NULL);
			break;
		default:
			LogCrit(COMPONENT_CONFIG,
				"Cannot set default for parameter %s, type(%d) yet",
				item->name, item->type);
			errors++;
			break;
		}
	}
	return errors;
}

static int do_block_load(struct config_node *blk,
			 struct config_item *params,
			 void *param_struct)
{
	struct config_item *item;
	caddr_t *param_addr;
	struct sockaddr *sock;
	struct config_node *node, *next_node = NULL;
	struct glist_head *ns;
	int rc;
	int errors = 0;

	for (item = params; item->name != NULL; item++) {
		int64_t val;
		uint64_t uval;
		uint32_t flags;
		bool bval;

		node = lookup_node(&blk->u.sub_nodes, item->name);
		while (node != NULL) {
			next_node = lookup_next_node(&blk->u.sub_nodes,
						     &node->node, item->name);
			if (next_node != NULL &&
			    (item->flags & CONFIG_UNIQUE)) {
				LogMajor(COMPONENT_CONFIG,
					 "At (%s:%d): Parameter %s set more than once",
					 next_node->filename,
					 next_node->linenumber,
					 next_node->name);
				errors++;
				node = next_node;
				continue;
			}
			param_addr = (caddr_t *)((uint64_t)param_struct + item->off);
			switch (item->type) {
			case CONFIG_INT16:
				if (convert_int(node, item->u.i16.minval,
						item->u.i16.maxval,
						&val))
					*(int16_t *)param_addr = (int16_t)val;
				else
					errors++;
				break;
			case CONFIG_UINT16:
				if (convert_uint(node, item->u.ui16.minval,
						 item->u.ui16.maxval,
						 &uval))
					*(uint16_t *)param_addr = (uint16_t)uval;
				else
					errors++;
				break;
			case CONFIG_INT32:
				if (convert_int(node, item->u.i32.minval,
						item->u.i32.maxval,
						&val))
					*(int32_t *)param_addr = (int32_t)val;
				else
					errors++;
				break;
			case CONFIG_UINT32:
				if (convert_uint(node, item->u.ui32.minval,
						 item->u.ui32.maxval,
						 &uval)) {
					if (item->flags & CONFIG_MODE)
						uval = unix2fsal_mode(uval);
					*(uint32_t *)param_addr = (uint32_t)uval;
				} else
					errors++;
				break;
			case CONFIG_INT64:
				if (convert_int(node, item->u.i64.minval,
						item->u.i64.maxval,
						&val))
					*(int64_t *)param_addr = val;
				else
					errors++;
				break;
			case CONFIG_UINT64:
				if (convert_uint(node, item->u.ui64.minval,
						 item->u.ui64.maxval,
						 &uval))
					*(uint64_t *)param_addr = uval;
				else
					errors++;
				break;
			case CONFIG_STRING:
				if (*(char **)param_addr != NULL)
					gsh_free(*(char **)param_addr);
				*(char **)param_addr =
					gsh_strdup(node->u.varvalue);
				break;
			case CONFIG_PATH:
				if (*(char **)param_addr != NULL)
					gsh_free(*(char **)param_addr);
				/** @todo validate path with access() */
				*(char **)param_addr =
					gsh_strdup(node->u.varvalue);
				break;
			case CONFIG_BOOL:
				if (convert_bool(node, &bval))
					*(bool *)param_addr = bval;
				else
					errors++;
				break;
			case CONFIG_LIST:
				if (item->u.lst.def == *(uint32_t *)param_addr)
					*(uint32_t *)param_addr = 0;
				rc = convert_list(node, item, &flags);
				if (rc == 0)
					*(uint32_t *)param_addr |= flags;
				else
					errors += rc;
				break;
			case CONFIG_ENUM:
				if (item->u.lst.def == *(uint32_t *)param_addr)
					*(uint32_t *)param_addr = 0;
				rc = convert_enum(node, item, &flags);
				if (rc == 0)
					*(uint32_t *)param_addr = flags;
				else
					errors += rc;
				break;
			case CONFIG_IPV4_ADDR:
				sock = (struct sockaddr *)param_addr;

				rc = convert_inet_addr(node, item,
						       AF_INET, sock);
				if (rc != 0)
					errors++;
				break;
			case CONFIG_IPV6_ADDR:
				sock = (struct sockaddr *)param_addr;

				rc = convert_inet_addr(node, item,
						       AF_INET6, sock);
				if (rc != 0)
					errors++;
				break;
			case CONFIG_INET_PORT:
				if (convert_uint(node, item->u.ui16.minval,
						 item->u.ui16.maxval,
						 &uval))
					*(uint16_t *)param_addr =
						htons((uint16_t)uval);
				else
					errors++;
				break;
			case CONFIG_BLOCK:
				rc = do_subblock(node, item, param_addr);
				if (rc != 0)
					errors++;
				break;
			default:
				LogCrit(COMPONENT_CONFIG,
					"Cannot set value for type(%d) yet",
					item->type);
				break;
			}
			node = next_node;
		}
	}
	/* We've been marking config nodes as being "seen" during the
	 * scans.  Report the bogus and typo inflicted bits.
	 */
	glist_for_each(ns, &blk->u.sub_nodes) {
		node = glist_entry(ns, struct config_node, node);
		if (node->found)
			node->found = false;
		else
			LogMajor(COMPONENT_CONFIG,
				 "At (%s:%d): Unknown parameter (%s)",
				 node->filename,
				 node->linenumber,
				 node->name);
	}
	return errors;
}

/**
 * @brief Fill configuration structure from parse tree
 *
 */

int load_config_from_parse(config_file_t config,
			   struct config_block *conf_blk,
			   void *param,
			   bool unique)
{
	struct config_root *tree = (struct config_root *)config;
	struct config_node *node;
	struct glist_head *ns;
	char *blkname = conf_blk->name;
	bool found = false;
	int rc, cum_errs = 0;

	cum_errs = do_block_init(conf_blk->params, param);
	glist_for_each(ns, &tree->nodes) {
		node = glist_entry(ns, struct config_node, node);
		if (node->type == TYPE_BLOCK &&
		    strcasecmp(blkname, node->name) == 0) {
			if (found && unique) {
				LogWarn(COMPONENT_CONFIG,
					"(%s:%d): Only one %s block allowed",
					node->filename,
					node->linenumber,
					blkname);
			} else {
				found = true;
				rc = do_block_load(node, conf_blk->params, param);
				if (rc != 0) {
					LogMajor(COMPONENT_CONFIG,
						 "Found %d errors in block %s",
						 rc,
						 blkname);
					cum_errs += rc;
				}
			}
		}
	}
	if (!found) {
		LogWarn(COMPONENT_CONFIG,
			 "Block %s not found. Using defaults", blkname);
	}
	if (cum_errs != 0)
		LogMajor(COMPONENT_CONFIG,
			 "%d errors found in configuration block %s",
			 cum_errs, blkname);
	return cum_errs;
}

/**
 * config_GetNbBlocks:
 * Indicates how many blocks are defined into the config file.
 */
int config_GetNbBlocks(config_file_t config)
{
	struct config_root *tree = (struct config_root *)config;
	struct config_node *node;
	int bcnt = 0, scnt = 0;
	struct glist_head *nsi, *nsn;

	if (glist_empty(&tree->nodes))
		return 0;
	glist_for_each_safe(nsi, nsn, &tree->nodes) {
		node = glist_entry(nsi, struct config_node, node);
		if (node->type == TYPE_BLOCK)
			bcnt++;
		else
			scnt++;
	}
	return bcnt + scnt;
}

/* retrieves a given block from the config file, from its index */
config_item_t config_GetBlockByIndex(config_file_t config,
				     unsigned int block_no)
{
	struct config_root *tree = (struct config_root *)config;
	struct config_node *node;
	int cnt = 0;
	struct glist_head *nsi, *nsn;

	if (glist_empty(&tree->nodes))
		return NULL;
	glist_for_each_safe(nsi, nsn, &tree->nodes) {
		node = glist_entry(nsi, struct config_node, node);
		if (/* node->type == TYPE_BLOCK && */ block_no == cnt)
			return (config_item_t)node;
		cnt++;
	}
	/* not found */
	return NULL;
}

/* Return the name of a block */
char *config_GetBlockName(config_item_t block)
{
	struct config_node *curr_block = (struct config_node *) block;

	assert(curr_block->type == TYPE_BLOCK);

	return curr_block->name;
}

/* Indicates how many items are defines in a block */
int config_GetNbItems(config_item_t block)
{
	struct config_node *node = (struct config_node *)block;
	struct config_node *sub_node;
	int bcnt = 0, scnt = 0;
	struct glist_head *nsi, *nsn;

	assert(node->type == TYPE_BLOCK);
	if (glist_empty(&node->u.sub_nodes))
		return 0;
	glist_for_each_safe(nsi, nsn, &node->u.sub_nodes) {
		sub_node = glist_entry(nsi, struct config_node, node);
		if (sub_node->type == TYPE_STMT)
			scnt++;
		else
			bcnt++;
	}
	return scnt + bcnt;
}

/* retrieves a given block from the config file, from its index
 */
config_item_t config_GetItemByIndex(config_item_t block,
				    unsigned int item_no)
{
	struct config_node *node = (struct config_node *)block;
	struct config_node *sub_node;
	int cnt = 0;
	struct glist_head *nsi, *nsn;

	if (glist_empty(&node->u.sub_nodes))
		return NULL;
	glist_for_each_safe(nsi, nsn, &node->u.sub_nodes) {
		sub_node = glist_entry(nsi, struct config_node, node);
		if (/* sub_node->type == TYPE_STMT &&  */item_no == cnt)
			return (config_item_t)sub_node;
		cnt++;
	}
	/* not found */
	return NULL;
}

/* indicates which type of item it is */
config_item_type config_ItemType(config_item_t item)
{
	struct config_node *node = (struct config_node *)item;

	if (node->type == TYPE_BLOCK)
		return CONFIG_ITEM_BLOCK;
	else if (node->type == TYPE_STMT)
		return CONFIG_ITEM_VAR;
	else
		return 0;
}

/* Retrieves a key-value peer from a CONFIG_ITEM_VAR */
int config_GetKeyValue(config_item_t item, char **var_name, char **var_value)
{
	struct config_node *node = (struct config_node *)item;

	assert(node->type == TYPE_STMT);

	*var_name = node->name;
	*var_value = node->u.varvalue;

	return 0;
}

static config_item_t find_by_name(struct config_node *node, char *name)
{
	struct glist_head *nsi, *nsn;
	char *separ;
	config_item_t found_item = NULL;

	if (node->type != TYPE_BLOCK || glist_empty(&node->u.sub_nodes))
		return NULL;
	separ = strstr(name, "::");
	if (separ != NULL) {
		*separ++ = '\0';
		*separ++ = '\0';
	}
	glist_for_each_safe(nsi, nsn, &node->u.sub_nodes) {
		node = glist_entry(nsi, struct config_node, node);
		if (strcasecmp(node->name, name) == 0) {
			if (separ == NULL)
				found_item = (config_item_t)node;
			else
				found_item = find_by_name(node, separ);
			break;
		}
	}
	return found_item;
}

config_item_t config_FindItemByName(config_file_t config, const char *name)
{
	struct config_root *tree = (struct config_root *)config;
	struct config_node *node;
	struct glist_head *nsi, *nsn;
	char *separ, *tmpname, *current;
	config_item_t found_item = NULL;

	if (glist_empty(&tree->nodes))
		return NULL;
	tmpname = gsh_strdup(name);
	current = tmpname;
	separ = strstr(current, "::");
	if (separ != NULL) {
		*separ++ = '\0';
		*separ++ = '\0';
	}
	glist_for_each_safe(nsi, nsn, &tree->nodes) {
		node = glist_entry(nsi, struct config_node, node);
		if (strcasecmp(node->name, current) == 0) {
			if (separ == NULL)
				found_item = (config_item_t)node;
			else
				found_item = find_by_name(node, separ);
			break;
		}
	}
	gsh_free(tmpname);
	return found_item;
}

/* Directly returns the value of the key with the specified name.
 * This name can be "BLOCK::SUBBLOCK::SUBBLOCK::VARNAME"
 */
char *config_FindKeyValueByName(config_file_t config, const char *key_name)
{
	struct config_node *node;

	node = (struct config_node *) config_FindItemByName(config, key_name);

	assert(node->type == TYPE_STMT);
	return node->u.varvalue;

}

/* Directly returns the value of the key with the specified name
 * relative to the given block.  this is bad.  will segv on no name...
 */
char *config_GetKeyValueByName(config_item_t block, const char *key_name)
{
	struct config_node *node = (struct config_node *)block;
	char *name;

	name = gsh_strdup(key_name);
	if (name == NULL)
		return NULL;
	node = (struct config_node *)find_by_name(node, name);
	gsh_free(name);
	assert(node->type == TYPE_STMT);
	return node->u.varvalue;
}
