/*
 * libuci - Library for the Unified Configuration Interface
 * Copyright (C) 2008 Felix Fietkau <nbd@openwrt.org>
 *
 * this program is free software; you can redistribute it and/or modify
 * it under the terms of the gnu lesser general public license version 2.1
 * as published by the free software foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LIBUCI_H
#define __LIBUCI_H

#include <setjmp.h>
#include <stdio.h>

#define UCI_CONFDIR "/etc/config"

enum
{
	UCI_OK = 0,
	UCI_ERR_MEM,
	UCI_ERR_INVAL,
	UCI_ERR_NOTFOUND,
	UCI_ERR_IO,
	UCI_ERR_PARSE,
	UCI_ERR_UNKNOWN,
	UCI_ERR_LAST
};

struct uci_list
{
	void *next;
	void *prev;
};

struct uci_package;
struct uci_section;
struct uci_option;
struct uci_history;
struct uci_parse_context;


/**
 * uci_alloc: Allocate a new uci context
 */
extern struct uci_context *uci_alloc(void);

/**
 * uci_free: Free the uci context including all of its data
 */
extern void uci_free(struct uci_context *ctx);

/**
 * uci_perror: Print the last uci error that occured
 * @ctx: uci context
 * @str: string to print before the error message
 */
extern void uci_perror(struct uci_context *ctx, const char *str);

/**
 * uci_import: Import uci config data from a stream
 * @ctx: uci context
 * @stream: file stream to import from
 * @name: (optional) assume the config has the given name
 * @package: (optional) store the last parsed config package in this variable
 *
 * the name parameter is for config files that don't explicitly use the 'package <...>' keyword
 */
extern int uci_import(struct uci_context *ctx, FILE *stream, const char *name, struct uci_package **package);

/**
 * uci_export: Export one or all uci config packages
 * @ctx: uci context
 * @stream: output stream
 * @package: (optional) uci config package to export
 */
extern int uci_export(struct uci_context *ctx, FILE *stream, struct uci_package *package);

/**
 * uci_load: Parse an uci config file and store it in the uci context
 *
 * @ctx: uci context
 * @name: name of the config file (relative to the config directory)
 * @package: store the loaded config package in this variable
 */
extern int uci_load(struct uci_context *ctx, const char *name, struct uci_package **package);

/**
 * uci_unload: Unload a config file from the uci context
 *
 * @ctx: uci context
 * @name: name of the config file
 */
extern int uci_unload(struct uci_context *ctx, const char *name);

/**
 * uci_cleanup: Clean up after an error
 *
 * @ctx: uci context
 */
extern int uci_cleanup(struct uci_context *ctx);

/**
 * uci_list_configs: List available uci config files
 *
 * @ctx: uci context
 */
extern char **uci_list_configs();

/* UCI data structures */

struct uci_context
{
	/* list of config packages */
	struct uci_list root;

	/* parser context, use for error handling only */
	struct uci_parse_context *pctx;

	/* private: */
	int errno;
	jmp_buf trap;
	char *buf;
	int bufsz;
};

struct uci_parse_context
{
	/* error context */
	const char *reason;
	int line;
	int byte;

	/* private: */
	struct uci_package *package;
	struct uci_section *section;
	FILE *file;
	const char *name;
	char *buf;
	int bufsz;
};

struct uci_package
{
	struct uci_list list;
	struct uci_list sections;
	struct uci_context *ctx;
	char *name;
	/* private: */
	int n_section;
};

struct uci_section
{
	struct uci_list list;
	struct uci_list options;
	struct uci_package *package;
	char *type;
	char *name;
};

struct uci_option
{
	struct uci_list list;
	struct uci_section *section;
	char *name;
	char *value;
};

enum uci_type {
	UCI_TYPE_PACKAGE,
	UCI_TYPE_SECTION,
	UCI_TYPE_OPTION
};

enum uci_command {
	UCI_CMD_ADD,
	UCI_CMD_REMOVE,
	UCI_CMD_CHANGE
};

struct uci_history
{
	struct uci_list list;
	enum uci_command cmd;
	enum uci_type type;
	union {
		struct {
			char *name;
		} p;
		struct {
			char *type;
			char *name;
		} c;
		struct {
			char *name;
			char *value;
		} o;
	} data;
};

/* linked list handling */
#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

/* returns true if a list is empty */
#define uci_list_empty(list) ((list)->next == (list))

/**
 * uci_list_entry: casts an uci_list pointer to the containing struct.
 * @_type: config, section or option
 * @_ptr: pointer to the uci_list struct
 */
#define uci_list_entry(_type, _ptr) \
	((struct uci_ ## _type *) ((char *)(_ptr) - offsetof(struct uci_ ## _type,list)))

/**
 * uci_foreach_entry: loop through a list of configs, sections or options
 * @_type: see uci_list_entry
 * @_list: pointer to the uci_list struct
 * @_ptr: iteration variable
 *
 * use like a for loop, e.g:
 *   uci_foreach(section, &list, p) {
 *   	...
 *   }
 */
#define uci_foreach_entry(_type, _list, _ptr)		\
	for(_ptr = uci_list_entry(_type, (_list)->next);	\
		&_ptr->list != (_list);			\
		_ptr = uci_list_entry(_type, _ptr->list.next))

#endif
