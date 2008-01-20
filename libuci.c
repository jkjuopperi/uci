/*
 * libuci - Library for the Unified Configuration Interface
 * Copyright (C) 2008 Felix Fietkau <nbd@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * This file contains some common code for the uci library
 */

#include <sys/types.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "uci.h"

#define DEBUG
#include "err.h"

static const char *uci_errstr[] = {
	[UCI_OK] =           "Success",
	[UCI_ERR_MEM] =      "Out of memory",
	[UCI_ERR_INVAL] =    "Invalid argument",
	[UCI_ERR_NOTFOUND] = "Entry not found",
	[UCI_ERR_IO] =       "I/O error",
	[UCI_ERR_PARSE] =    "Parse error",
	[UCI_ERR_UNKNOWN] =  "Unknown error",
};


/*
 * UCI wrapper for malloc, which uses exception handling
 */
static void *uci_malloc(struct uci_context *ctx, size_t size)
{
	void *ptr;

	ptr = malloc(size);
	if (!ptr)
		UCI_THROW(ctx, UCI_ERR_MEM);
	memset(ptr, 0, size);

	return ptr;
}

/*
 * UCI wrapper for realloc, which uses exception handling
 */
static void *uci_realloc(struct uci_context *ctx, void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (!ptr)
		UCI_THROW(ctx, UCI_ERR_MEM);

	return ptr;
}

/*
 * UCI wrapper for strdup, which uses exception handling
 */
static char *uci_strdup(struct uci_context *ctx, const char *str)
{
	char *ptr;

	ptr = strdup(str);
	if (!ptr)
		UCI_THROW(ctx, UCI_ERR_MEM);

	return ptr;
}

#include "list.c"
#include "parse.c"

/* externally visible functions */

struct uci_context *uci_alloc(void)
{
	struct uci_context *ctx;

	ctx = (struct uci_context *) malloc(sizeof(struct uci_context));
	memset(ctx, 0, sizeof(struct uci_context));
	uci_list_init(&ctx->root);

	return ctx;
}

int uci_cleanup(struct uci_context *ctx)
{
	UCI_HANDLE_ERR(ctx);
	uci_parse_cleanup(ctx);
	return 0;
}

void uci_perror(struct uci_context *ctx, const char *str)
{
	int err;

	if (!ctx)
		err = UCI_ERR_INVAL;
	else
		err = ctx->errno;

	if ((err < 0) || (err >= UCI_ERR_LAST))
		err = UCI_ERR_UNKNOWN;

	switch (err) {
	case UCI_ERR_PARSE:
		if (ctx->pctx) {
			fprintf(stderr, "%s: %s at line %d, byte %d\n", str, uci_errstr[err], ctx->pctx->line, ctx->pctx->byte);
			break;
		}
		/* fall through */
	default:
		fprintf(stderr, "%s: %s\n", str, uci_errstr[err]);
		break;
	}
}


