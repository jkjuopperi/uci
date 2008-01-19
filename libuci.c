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

#include "libuci.h"

#define DEBUG

#ifdef DEBUG
#define DPRINTF(...) fprintf(stderr, __VA_ARGS__)
#else
#define DPRINTF(...)
#endif
/* 
 * throw an uci exception and store the error number
 * in the context.
 */
#define UCI_THROW(ctx, err) do { 	\
	longjmp(ctx->trap, err); 	\
} while (0)

/*
 * store the return address for handling exceptions
 * needs to be called in every externally visible library function
 *
 * NB: this does not handle recursion at all. Calling externally visible
 * functions from other uci functions is only allowed at the end of the
 * calling function.
 */
#define UCI_HANDLE_ERR(ctx) do { 		\
	int __val;			\
	if (!ctx)			\
		return UCI_ERR_INVAL;	\
	__val = setjmp(ctx->trap);	\
	if (__val) {			\
		ctx->errno = __val;	\
		return __val;		\
	}				\
} while (0)

/*
 * check the specified condition.
 * throw an invalid argument exception if it's false
 */
#define UCI_ASSERT(ctx, expr) do {	\
	if (!(expr)) {			\
		DPRINTF("[%s:%d] Assertion failed\n", __FILE__, __LINE__); \
		UCI_THROW(ctx, UCI_ERR_INVAL);	\
	}				\
} while (0)


static char *uci_errstr[] = {
	[UCI_OK] =           "Success",
	[UCI_ERR_MEM] =      "Out of memory",
	[UCI_ERR_INVAL] =    "Invalid argument",
	[UCI_ERR_NOTFOUND] = "Entry not found",
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

#include "hash.c"
#include "parse.c"

/* externally visible functions */

struct uci_context *uci_alloc(void)
{
	struct uci_context *ctx;
	
	ctx = (struct uci_context *) malloc(sizeof(struct uci_context));
	memset(ctx, 0, sizeof(struct uci_context));
	
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


