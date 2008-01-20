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

struct uci_config;
struct uci_section;
struct uci_option;
struct uci_parse_context;


/**
 * uci_alloc: Allocate a new uci context
 */
extern struct uci_context *uci_alloc(void);

/**
 * uci_perror: Print the last uci error that occured
 * @ctx: uci context
 * @str: string to print before the error message
 */
extern void uci_perror(struct uci_context *ctx, const char *str);

/**
 * uci_load: Parse an uci config file and store it in the uci context
 *
 * @ctx: uci context
 * @name: name of the config file (relative to the config directory)
 */
int uci_load(struct uci_context *ctx, const char *name);

/**
 * uci_cleanup: Clean up after an error
 *
 * @ctx: uci context
 */
int uci_cleanup(struct uci_context *ctx);


/* UCI data structures */

struct uci_context
{
	struct uci_list root;

	/* for error handling only */
	struct uci_parse_context *pctx;

	/* private: */
	int errno;
	jmp_buf trap;
};

struct uci_parse_context
{
	int line;
	int byte;

	/* private: */
	struct uci_config *cfg;
	FILE *file;
	char *buf;
	char *reason;
	int bufsz;
};

struct uci_config
{
	struct uci_list list;
	struct uci_list sections;
	struct uci_context *ctx;
	char *name;
};

struct uci_section
{
	struct uci_list list;
	struct uci_list options;
	struct uci_config *config;
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

/* linked list handling */
#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#define uci_list_empty(list) (list->next == ptr)
#define uci_list_entry(_type, _ptr) \
	((struct uci_ ## _type *) ((char *)(_ptr) - offsetof(struct uci_ ## _type,list)))

#define uci_foreach_entry(_type, _list, _ptr)		\
	for(_ptr = uci_list_entry(_type, (_list)->next);	\
		&_ptr->list != (_list);			\
		_ptr = uci_list_entry(_type, _ptr->list.next))

#endif
