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
 * This file contains wrappers to standard functions, which
 * throw exceptions upon failure.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <stdbool.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>

#define LINEBUF	32
#define LINEBUF_MAX	4096

static void *uci_malloc(struct uci_context *ctx, size_t size)
{
	void *ptr;

	ptr = malloc(size);
	if (!ptr)
		UCI_THROW(ctx, UCI_ERR_MEM);
	memset(ptr, 0, size);

	return ptr;
}

static void *uci_realloc(struct uci_context *ctx, void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (!ptr)
		UCI_THROW(ctx, UCI_ERR_MEM);

	return ptr;
}

static char *uci_strdup(struct uci_context *ctx, const char *str)
{
	char *ptr;

	ptr = strdup(str);
	if (!ptr)
		UCI_THROW(ctx, UCI_ERR_MEM);

	return ptr;
}

static bool uci_validate_name(const char *str)
{
	if (!*str)
		return false;

	while (*str) {
		if (!isalnum(*str) && (*str != '_'))
			return false;
		str++;
	}
	return true;
}

static void uci_alloc_parse_context(struct uci_context *ctx)
{
	ctx->pctx = (struct uci_parse_context *) uci_malloc(ctx, sizeof(struct uci_parse_context));
}

int uci_parse_tuple(struct uci_context *ctx, char *str, char **package, char **section, char **option, char **value)
{
	char *last = NULL;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, str && package && section && option);

	*package = strtok(str, ".");
	if (!*package || !uci_validate_name(*package))
		goto error;

	last = *package;
	*section = strtok(NULL, ".");
	if (!*section)
		goto lastval;

	last = *section;
	*option = strtok(NULL, ".");
	if (!*option)
		goto lastval;

	last = *option;

lastval:
	last = strchr(last, '=');
	if (last) {
		if (!value)
			goto error;

		*last = 0;
		last++;
		if (!*last)
			goto error;
		*value = last;
	}

	if (*section && !uci_validate_name(*section))
		goto error;
	if (*option && !uci_validate_name(*option))
		goto error;

	goto done;

error:
	UCI_THROW(ctx, UCI_ERR_PARSE);

done:
	return 0;
}


static void uci_parse_error(struct uci_context *ctx, char *pos, char *reason)
{
	struct uci_parse_context *pctx = ctx->pctx;

	pctx->reason = reason;
	pctx->byte = pos - pctx->buf;
	UCI_THROW(ctx, UCI_ERR_PARSE);
}


/*
 * Fetch a new line from the input stream and resize buffer if necessary
 */
static void uci_getln(struct uci_context *ctx, int offset)
{
	struct uci_parse_context *pctx = ctx->pctx;
	char *p;
	int ofs;

	if (pctx->buf == NULL) {
		pctx->buf = uci_malloc(ctx, LINEBUF);
		pctx->bufsz = LINEBUF;
	}

	ofs = offset;
	do {
		p = &pctx->buf[ofs];
		p[ofs] = 0;

		p = fgets(p, pctx->bufsz - ofs, pctx->file);
		if (!p || !*p)
			return;

		ofs += strlen(p);
		if (pctx->buf[ofs - 1] == '\n') {
			pctx->line++;
			pctx->buf[ofs - 1] = 0;
			return;
		}

		if (pctx->bufsz > LINEBUF_MAX/2)
			uci_parse_error(ctx, p, "line too long");

		pctx->bufsz *= 2;
		pctx->buf = uci_realloc(ctx, pctx->buf, pctx->bufsz);
	} while (1);
}

/*
 * open a stream and go to the right position
 *
 * note: when opening for write and seeking to the beginning of
 * the stream, truncate the file
 */
static FILE *uci_open_stream(struct uci_context *ctx, const char *filename, int pos, bool write, bool create)
{
	struct stat statbuf;
	FILE *file = NULL;
	int fd, ret;
	int mode = (write ? O_RDWR : O_RDONLY);

	if (create)
		mode |= O_CREAT;

	if (!write && ((stat(filename, &statbuf) < 0) ||
		((statbuf.st_mode &  S_IFMT) != S_IFREG))) {
		UCI_THROW(ctx, UCI_ERR_NOTFOUND);
	}

	fd = open(filename, mode, UCI_FILEMODE);
	if (fd <= 0)
		goto error;

	if (flock(fd, (write ? LOCK_EX : LOCK_SH)) < 0)
		goto error;

	ret = lseek(fd, 0, pos);

	if (ret < 0)
		goto error;

	file = fdopen(fd, (write ? "w+" : "r"));
	if (file)
		goto done;

error:
	UCI_THROW(ctx, UCI_ERR_IO);
done:
	return file;
}

static void uci_close_stream(FILE *stream)
{
	int fd;

	if (!stream)
		return;

	fd = fileno(stream);
	flock(fd, LOCK_UN);
	fclose(stream);
}


