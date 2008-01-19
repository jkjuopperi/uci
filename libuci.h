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

enum
{
	UCI_OK = 0,
	UCI_ERR_MEM,
	UCI_ERR_INVAL,
	UCI_ERR_NOTFOUND,
	UCI_ERR_PARSE,
	UCI_ERR_UNKNOWN,
	UCI_ERR_LAST
};

struct uci_config;
struct uci_parse_context;

struct uci_context
{
	struct uci_config *root;
	
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
	FILE *file;
	char *buf;
	int bufsz;
};


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
 * uci_parse: Parse an uci config file and store it in the uci context
 *
 * @ctx: uci context
 * @name: name of the config file (relative to the config directory)
 */
int uci_parse(struct uci_context *ctx, const char *name);

/**
 * uci_cleanup: Clean up after an error
 *
 * @ctx: uci context
 */
int uci_cleanup(struct uci_context *ctx);

#endif
