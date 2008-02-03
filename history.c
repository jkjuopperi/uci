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
 * This file contains the code for handling uci config history files
 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <ctype.h>

int uci_set_savedir(struct uci_context *ctx, const char *dir)
{
	char *sdir;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, dir != NULL);

	sdir = uci_strdup(ctx, dir);
	if (ctx->savedir != uci_savedir)
		free(ctx->savedir);
	ctx->savedir = sdir;
	return 0;
}

int uci_add_history_path(struct uci_context *ctx, const char *dir)
{
	struct uci_element *e;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, dir != NULL);
	e = uci_alloc_generic(ctx, UCI_TYPE_PATH, dir, sizeof(struct uci_element));
	uci_list_add(&ctx->history_path, &e->list);

	return 0;
}

static void uci_parse_history_line(struct uci_context *ctx, struct uci_package *p, char *buf)
{
	bool delete = false;
	bool rename = false;
	char *package = NULL;
	char *section = NULL;
	char *option = NULL;
	char *value = NULL;

	if (buf[0] == '-') {
		delete = true;
		buf++;
	} else if (buf[0] == '@') {
		rename = true;
		buf++;
	}

	UCI_INTERNAL(uci_parse_tuple, ctx, buf, &package, &section, &option, &value);
	if (!package || (strcmp(package, p->e.name) != 0))
		goto error;
	if (!uci_validate_name(section))
		goto error;
	if (option && !uci_validate_name(option))
		goto error;
	if ((rename || !delete) && !uci_validate_name(value))
		goto error;

	if (rename)
		UCI_INTERNAL(uci_rename, ctx, p, section, option, value);
	else if (delete)
		UCI_INTERNAL(uci_delete, ctx, p, section, option);
	else
		UCI_INTERNAL(uci_set, ctx, p, section, option, value);

	return;
error:
	UCI_THROW(ctx, UCI_ERR_PARSE);
}

static void uci_parse_history(struct uci_context *ctx, FILE *stream, struct uci_package *p)
{
	struct uci_parse_context *pctx;

	/* make sure no memory from previous parse attempts is leaked */
	ctx->internal = true;
	uci_cleanup(ctx);

	pctx = (struct uci_parse_context *) uci_malloc(ctx, sizeof(struct uci_parse_context));
	ctx->pctx = pctx;
	pctx->file = stream;

	while (!feof(pctx->file)) {
		uci_getln(ctx, 0);
		if (!pctx->buf[0])
			continue;

		/*
		 * ignore parse errors in single lines, we want to preserve as much
		 * history as possible
		 */
		UCI_TRAP_SAVE(ctx, error);
		uci_parse_history_line(ctx, p, pctx->buf);
		UCI_TRAP_RESTORE(ctx);
error:
		continue;
	}

	/* no error happened, we can get rid of the parser context now */
	ctx->internal = true;
	uci_cleanup(ctx);
}

static void uci_load_history_file(struct uci_context *ctx, struct uci_package *p, char *filename, FILE **f, bool flush)
{
	FILE *stream = NULL;

	UCI_TRAP_SAVE(ctx, done);
	stream = uci_open_stream(ctx, filename, SEEK_SET, flush, false);
	if (p)
		uci_parse_history(ctx, stream, p);
	UCI_TRAP_RESTORE(ctx);
done:
	if (f)
		*f = stream;
	else if (stream)
		uci_close_stream(stream);
}

static void uci_load_history(struct uci_context *ctx, struct uci_package *p, bool flush)
{
	struct uci_element *e;
	char *filename = NULL;
	FILE *f = NULL;

	if (!p->confdir)
		return;

	uci_foreach_element(&ctx->history_path, e) {
		if ((asprintf(&filename, "%s/%s", e->name, p->e.name) < 0) || !filename)
			UCI_THROW(ctx, UCI_ERR_MEM);

		uci_load_history_file(ctx, p, filename, NULL, false);
		free(filename);
	}

	if ((asprintf(&filename, "%s/%s", ctx->savedir, p->e.name) < 0) || !filename)
		UCI_THROW(ctx, UCI_ERR_MEM);

	uci_load_history_file(ctx, p, filename, &f, flush);
	if (flush && f) {
		rewind(f);
		ftruncate(fileno(f), 0);
	}
	if (filename)
		free(filename);
	uci_close_stream(f);
	ctx->errno = 0;
}

int uci_save(struct uci_context *ctx, struct uci_package *p)
{
	FILE *f = NULL;
	char *filename = NULL;
	struct uci_element *e, *tmp;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, p != NULL);

	/* 
	 * if the config file was outside of the /etc/config path,
	 * don't save the history to a file, update the real file
	 * directly.
	 * does not modify the uci_package pointer
	 */
	if (!p->confdir)
		return uci_commit(ctx, &p, false);

	if (uci_list_empty(&p->history))
		return 0;

	if ((asprintf(&filename, "%s/%s", ctx->savedir, p->e.name) < 0) || !filename)
		UCI_THROW(ctx, UCI_ERR_MEM);

	ctx->errno = 0;
	UCI_TRAP_SAVE(ctx, done);
	f = uci_open_stream(ctx, filename, SEEK_END, true, true);
	UCI_TRAP_RESTORE(ctx);

	uci_foreach_element_safe(&p->history, tmp, e) {
		struct uci_history *h = uci_to_history(e);

		if (h->cmd == UCI_CMD_REMOVE)
			fprintf(f, "-");
		else if (h->cmd == UCI_CMD_RENAME)
			fprintf(f, "@");

		fprintf(f, "%s.%s", p->e.name, h->section);
		if (e->name)
			fprintf(f, ".%s", e->name);

		if (h->cmd == UCI_CMD_REMOVE)
			fprintf(f, "\n");
		else
			fprintf(f, "=%s\n", h->value);
		uci_free_history(h);
	}

done:
	uci_close_stream(f);
	if (filename)
		free(filename);
	if (ctx->errno)
		UCI_THROW(ctx, ctx->errno);

	return 0;
}


