/* ----------------------------------------------------------------------------
 * Copyright CEA/DAM/DIF  (2007)
 * contributeur : Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * ---------------------------------------
 */
#ifndef _CONFIG_PARSING_H
#define _CONFIG_PARSING_H

#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

/* opaque type */
typedef caddr_t config_file_t;
typedef caddr_t config_item_t;

typedef enum { CONFIG_ITEM_BLOCK = 1, CONFIG_ITEM_VAR } config_item_type;

/**
 * @brief Data structures for config parse tree processing
 */

enum config_type {
	CONFIG_NULL = 0,
	CONFIG_INT16,
	CONFIG_UINT16,
	CONFIG_INT32,
	CONFIG_UINT32,
	CONFIG_INT64,
	CONFIG_UINT64,
	CONFIG_STRING,
	CONFIG_PATH,
	CONFIG_LIST,
	CONFIG_ENUM,
	CONFIG_TOKEN,
	CONFIG_BOOL,
	CONFIG_IPV4_ADDR,
	CONFIG_IPV6_ADDR,
	CONFIG_INET_PORT,
	CONFIG_BLOCK,
	CONFIG_PROC
};

#define CONFIG_UNIQUE		0x001
#define CONFIG_MANDATORY	0x002
#define CONFIG_MODE		0x004

struct config_block;
struct config_item;

/**
 * @brief token list for CSV options
 */

struct config_item_list {
	const char *token;
	uint32_t value;
};

#define CONFIG_LIST_TOK(_token_, _flags_) \
	{ .token = _token_, .value = _flags_}

#define CONFIG_LIST_EOL { .token = NULL, .value = 0}
/**
 * @brief A config file parameter
 *
 * These are structured as an initialized array with
 * CONFIG_EOL as the last initializer.
 */

struct config_item {
	char *name;
	enum config_type type;
	int flags;
	union {
		struct { /* CONFIG_BOOL */
			bool def;
		} b;
		struct { /* CONFIG_STRING | CONFIG_PATH */
			int minsize;
			int maxsize;
			const char *def;
		} str;
		struct { /* CONFIG_IPV4_ADDR */
			const char *def;
		} ipv4;
		struct { /* CONFIG_IPV6_ADDR */
			const char *def;
		} ipv6;
		struct { /* CONFIG_INT16 */
			int16_t minval;
			int16_t maxval;
			int16_t def;
		} i16;
		struct { /* CONFIG_UINT16 */
			uint16_t minval;
			uint16_t maxval;
			uint16_t def;
		} ui16;
		struct { /* CONFIG_INT32 */
			int64_t minval;
			int64_t maxval;
			int64_t def;
		} i32;
		struct { /* CONFIG_UINT32 */
			uint32_t minval;
			uint32_t maxval;
			uint32_t def;
		} ui32;
		struct { /* CONFIG_INT64 */
			int64_t minval;
			int64_t maxval;
			int64_t def;
		} i64;
		struct { /* CONFIG_UINT64 */
			uint64_t minval;
			uint64_t maxval;
			uint64_t def;
		} ui64;
		struct { /* CONFIG_LIST | CONFIG_ENUM */
			uint32_t def;
			struct config_item_list *tokens;
		} lst;
		struct { /* CONFIG_BLOCK */
			void *(*param_mem)(void *parent, void *child);
			struct config_item *sub_blk;
			void (*attach)(void *parent, void *child);
		} blk;
		struct { /* CONFIG_PROC */
			struct conf_item_list *tokens;
			uint32_t def;
			void (*setf)(void *);
		} proc;
	} u;
	size_t off; /* offset into struct pointed to by opaque_dest */
};

#define CONF_ITEM_BLOCK(_name_, _mem_, _params_, _attach_) \
	{ .name = _name_,			    \
	  .type = CONFIG_PROC,		    \
	  .u.blk.param_mem = _mem_,			    \
	  .u.blk.sub_blk = _params_,		    \
	  .u.blk.attach = _attach_,	    \
	  .off = 0L				\
	}

#define CONF_ITEM_PROC(_name_, _def_, _tokens_, _proc_) \
	{ .name = _name_,			    \
	  .type = CONFIG_PROC,		    \
	  .u.proc.def = _def_,			    \
	  .u.proc.tokens = _tokens_,		    \
	  .u.proc.setf = _proc_	    \
	}

#define CONF_ITEM_LIST(_name_, _def_, _tokens_, _struct_, _mem_) \
	{ .name = _name_,			    \
	  .type = CONFIG_LIST,		    \
	  .u.lst.def = _def_,			    \
	  .u.lst.tokens = _tokens_,		    \
	  .off = offsetof(struct _struct_, _mem_)   \
	}

#define CONF_ITEM_ENUM(_name_, _def_, _tokens_, _struct_, _mem_) \
	{ .name = _name_,			    \
	  .type = CONFIG_ENUM,		    \
	  .u.lst.def = _def_,			    \
	  .u.lst.tokens = _tokens_,		    \
	  .off = offsetof(struct _struct_, _mem_)   \
	}

#define CONF_UNIQ_ENUM(_name_, _def_, _tokens_, _struct_, _mem_) \
	{ .name = _name_,			    \
	  .type = CONFIG_ENUM,		    \
	  .flags = CONFIG_UNIQUE,		    \
	  .u.lst.def = _def_,			    \
	  .u.lst.tokens = _tokens_,		    \
	  .off = offsetof(struct _struct_, _mem_)   \
	}

#define CONF_ITEM_BOOL(_name_, _def_, _struct_, _mem_) \
	{ .name = _name_,			    \
	  .type = CONFIG_BOOL,		    \
	  .u.b.def = _def_,			    \
	  .off = offsetof(struct _struct_, _mem_)   \
	}

#define CONF_ITEM_STR(_name_, _minsize_, _maxsize_, _def_, _struct_, _mem_) \
	{ .name = _name_,			    \
	  .type = CONFIG_STRING,		    \
	  .u.str.minsize = _minsize_,		    \
	  .u.str.maxsize = _maxsize_,		    \
	  .u.str.def = _def_,			    \
	  .off = offsetof(struct _struct_, _mem_)   \
	}
#define CONF_ITEM_PATH(_name_, _minsize_, _maxsize_, _def_, _struct_, _mem_) \
	{ .name = _name_,			    \
	  .type = CONFIG_PATH,		    \
	  .u.str.minsize = _minsize_,		    \
	  .u.str.maxsize = _maxsize_,		    \
	  .u.str.def = _def_,			    \
	  .off = offsetof(struct _struct_, _mem_)   \
	}
#define CONF_ITEM_IPV4_ADDR(_name_, _def_, _struct_, _mem_) \
	{ .name = _name_,			    \
	  .type = CONFIG_IPV4_ADDR,		    \
	  .u.ipv4.def = _def_,			    \
	  .off = offsetof(struct _struct_, _mem_)   \
	}

#define CONF_ITEM_IPV6_ADDR(_name_, _def_, _struct_, _mem_) \
	{ .name = _name_,			    \
	  .type = CONFIG_IPV6_ADDR,		    \
	  .u.ipv6.def = _def_,			    \
	  .off = offsetof(struct _struct_, _mem_)   \
	}

#define CONF_ITEM_INET_PORT(_name_, _min_, _max_, _def_, _struct_, _mem_) \
	{ .name = _name_,			    \
	  .type = CONFIG_INET_PORT,		    \
	  .u.i16.minval = _min_,		    \
	  .u.i16.maxval = _max_,		    \
	  .u.i16.def = _def_,			    \
	  .off = offsetof(struct _struct_, _mem_)   \
	}

#define CONF_ITEM_UI16(_name_, _min_, _max_, _def_, _struct_, _mem_) \
	{ .name = _name_,			    \
	  .type = CONFIG_UINT16,		    \
	  .u.ui16.minval = _min_,		    \
	  .u.ui16.maxval = _max_,		    \
	  .u.ui16.def = _def_,			    \
	  .off = offsetof(struct _struct_, _mem_)   \
	}

#define CONF_ITEM_UI16(_name_, _min_, _max_, _def_, _struct_, _mem_) \
	{ .name = _name_,			    \
	  .type = CONFIG_UINT16,		    \
	  .u.ui16.minval = _min_,		    \
	  .u.ui16.maxval = _max_,		    \
	  .u.ui16.def = _def_,			    \
	  .off = offsetof(struct _struct_, _mem_)   \
	}

#define CONF_ITEM_I32(_name_, _min_, _max_, _def_, _struct_, _mem_) \
	{ .name = _name_,			    \
	  .type = CONFIG_INT32,		    \
	  .u.i32.minval = _min_,		    \
	  .u.i32.maxval = _max_,		    \
	  .u.i32.def = _def_,			    \
	  .off = offsetof(struct _struct_, _mem_)   \
	}

#define CONF_ITEM_UI32(_name_, _min_, _max_, _def_, _struct_, _mem_) \
	{ .name = _name_,			    \
	  .type = CONFIG_UINT32,		    \
	  .u.ui32.minval = _min_,		    \
	  .u.ui32.maxval = _max_,		    \
	  .u.ui32.def = _def_,			    \
	  .off = offsetof(struct _struct_, _mem_)   \
	}

#define CONF_ITEM_MODE(_name_, _min_, _max_, _def_, _struct_, _mem_) \
	{ .name = _name_,			    \
	  .type = CONFIG_UINT32,		    \
	  .flags = CONFIG_MODE,			    \
	  .u.ui32.minval = _min_,		    \
	  .u.ui32.maxval = _max_,		    \
	  .u.ui32.def = _def_,			    \
	  .off = offsetof(struct _struct_, _mem_)   \
	}

#define CONF_ITEM_I64(_name_, _min_, _max_, _def_, _struct_, _mem_) \
	{ .name = _name_,			    \
	  .type = CONFIG_INT64,		    \
	  .u.i64.minval = _min_,		    \
	  .u.i64.maxval = _max_,		    \
	  .u.i64.def = _def_,			    \
	  .off = offsetof(struct _struct_, _mem_)   \
	}

#define CONF_ITEM_UI64(_name_, _min_, _max_, _def_, _struct_, _mem_) \
	{ .name = _name_,			    \
	  .type = CONFIG_UINT64,		    \
	  .u.ui64.minval = _min_,		    \
	  .u.ui64.maxval = _max_,		    \
	  .u.ui64.def = _def_,			    \
	  .off = offsetof(struct _struct_, _mem_)   \
	}

#define CONFIG_EOL {.name = NULL, .type = CONFIG_NULL}

/**
 * @brief Configuration Block
 *
 * This is used for both config file parse tree processing
 * and DBus property settings.
 */

struct config_block {
	char *name;
	char *dbus_interface_name;
	struct config_item *params;
};
	
	

/* config_ParseFile:
 * Reads the content of a configuration file and
 * stores it in a memory structure.
 * \return NULL on error.
 */
config_file_t config_ParseFile(char *file_path);

/* If config_ParseFile returns a NULL pointer,
 * config_GetErrorMsg returns a detailled message
 * to indicate the reason for this error.
 */
char *config_GetErrorMsg();

/**
 * config_Print:
 * Print the content of the syntax tree
 * to a file.
 */
void config_Print(FILE *output, config_file_t config);

/* Free the memory structure that store the configuration. */
void config_Free(config_file_t config);

int load_config_from_parse(config_file_t config,
			   struct config_block *conf_blk,
			   void *param,
			   bool unique);
/*
 * Old, deprecated API.  Will disappear
 */

/* Indicates how many main blocks are defined into the config file.
 * \return A positive value if no error.
 *         Else return a negative error code.
 */
int config_GetNbBlocks(config_file_t config);

/* retrieves a given block from the config file, from its index */
config_item_t config_GetBlockByIndex(config_file_t config,
				     unsigned int block_no);

/* Return the name of a block */
char *config_GetBlockName(config_item_t block);

/* Indicates how many items are defines in a block */
int config_GetNbItems(config_item_t block);

/* Retrieves an item from a given block and the subitem index. */
config_item_t config_GetItemByIndex(config_item_t block, unsigned int item_no);

/* indicates which type of item it is */
config_item_type config_ItemType(config_item_t item);

/* Retrieves a key-value peer from a CONFIG_ITEM_VAR */
int config_GetKeyValue(config_item_t item, char **var_name, char **var_value);

/* Returns a block or variable with the specified name. This name can be
 * "BLOCK::SUBBLOCK::SUBBLOCK"
 */
config_item_t config_FindItemByName(config_file_t config, const char *name);

/* Directly returns the value of the key with the specified name.
 * This name can be "BLOCK::SUBBLOCK::SUBBLOCK::VARNAME"
 */
char *config_FindKeyValueByName(config_file_t config, const char *key_name);

/* Directly returns the value of the key with the specified name
 * relative to the given block.
 */
char *config_GetKeyValueByName(config_item_t block, const char *key_name);

#endif
