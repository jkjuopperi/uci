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

#define _GNU_SOURCE
#include <sys/types.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "uci.h"
#include "err.h"

static const char *uci_confdir = UCI_CONFDIR;
static const char *uci_savedir = UCI_SAVEDIR;

static const char *uci_errstr[] = {
	[UCI_OK] =            "Success",
	[UCI_ERR_MEM] =       "Out of memory",
	[UCI_ERR_INVAL] =     "Invalid argument",
	[UCI_ERR_NOTFOUND] =  "Entry not found",
	[UCI_ERR_IO] =        "I/O error",
	[UCI_ERR_PARSE] =     "Parse error",
	[UCI_ERR_DUPLICATE] = "Duplicate entry",
	[UCI_ERR_UNKNOWN] =   "Unknown error",
};

static void uci_cleanup(struct uci_context *ctx);

#include "uci_internal.h"
#include "util.c"
#include "list.c"
#include "history.c"
#include "file.c"

/* exported functions */
struct uci_context *uci_alloc_context(void)
{
	struct uci_context *ctx;

	ctx = (struct uci_context *) malloc(sizeof(struct uci_context));
	memset(ctx, 0, sizeof(struct uci_context));
	uci_list_init(&ctx->root);
	uci_list_init(&ctx->history_path);
	uci_list_init(&ctx->backends);
	ctx->flags = UCI_FLAG_STRICT;

	ctx->confdir = (char *) uci_confdir;
	ctx->savedir = (char *) uci_savedir;

	uci_list_add(&ctx->backends, &uci_file_backend.e.list);
	ctx->backend = &uci_file_backend;

	return ctx;
}

void uci_free_context(struct uci_context *ctx)
{
	struct uci_element *e, *tmp;

	if (ctx->confdir != uci_confdir)
		free(ctx->confdir);
	if (ctx->savedir != uci_savedir)
		free(ctx->savedir);

	uci_cleanup(ctx);
	UCI_TRAP_SAVE(ctx, ignore);
	uci_foreach_element_safe(&ctx->root, tmp, e) {
		struct uci_package *p = uci_to_package(e);
		uci_free_package(&p);
	}
	uci_foreach_element_safe(&ctx->history_path, tmp, e) {
		uci_free_element(e);
	}
	free(ctx);
	UCI_TRAP_RESTORE(ctx);

ignore:
	return;
}

int uci_set_confdir(struct uci_context *ctx, const char *dir)
{
	char *cdir;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, dir != NULL);

	cdir = uci_strdup(ctx, dir);
	if (ctx->confdir != uci_confdir)
		free(ctx->confdir);
	ctx->confdir = cdir;
	return 0;
}

static void uci_cleanup(struct uci_context *ctx)
{
	struct uci_parse_context *pctx;

	if (ctx->buf) {
		free(ctx->buf);
		ctx->buf = NULL;
		ctx->bufsz = 0;
	}

	pctx = ctx->pctx;
	if (!pctx)
		return;

	ctx->pctx = NULL;
	if (pctx->package)
		uci_free_package(&pctx->package);

	if (pctx->buf)
		free(pctx->buf);

	free(pctx);
}

void uci_perror(struct uci_context *ctx, const char *prefix)
{
	int err;

	if (!ctx)
		err = UCI_ERR_INVAL;
	else
		err = ctx->errno;

	if ((err < 0) || (err >= UCI_ERR_LAST))
		err = UCI_ERR_UNKNOWN;

	if (prefix)
		fprintf(stderr, "%s: ", prefix);
	if (ctx->func)
		fprintf(stderr, "%s: ", ctx->func);

	switch (err) {
	case UCI_ERR_PARSE:
		if (ctx->pctx) {
			fprintf(stderr, "%s (%s) at line %d, byte %d\n", uci_errstr[err], (ctx->pctx->reason ? ctx->pctx->reason : "unknown"), ctx->pctx->line, ctx->pctx->byte);
			break;
		}
		/* fall through */
	default:
		fprintf(stderr, "%s\n", uci_errstr[err]);
		break;
	}
}

int uci_list_configs(struct uci_context *ctx, char ***list)
{
	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, list != NULL);
	UCI_ASSERT(ctx, ctx->backend && ctx->backend->list_configs);
	*list = ctx->backend->list_configs(ctx);
	return 0;
}

int uci_commit(struct uci_context *ctx, struct uci_package **package, bool overwrite)
{
	struct uci_package *p;
	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, package != NULL);
	p = *package;
	UCI_ASSERT(ctx, p != NULL);
	UCI_ASSERT(ctx, p->backend && p->backend->commit);
	p->backend->commit(ctx, package, overwrite);
	return 0;
}

int uci_load(struct uci_context *ctx, const char *name, struct uci_package **package)
{
	struct uci_package *p;
	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, ctx->backend && ctx->backend->load);
	p = ctx->backend->load(ctx, name);
	if (package)
		*package = p;

	return 0;
}

int uci_set_backend(struct uci_context *ctx, const char *name)
{
	struct uci_element *e;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, name != NULL);
	e = uci_lookup_list(&ctx->backends, name);
	if (!e)
		UCI_THROW(ctx, UCI_ERR_NOTFOUND);
	ctx->backend = uci_to_backend(e);
	return 0;
}
