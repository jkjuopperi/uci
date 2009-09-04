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
 * This file contains the code for parsing uci config files
 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/file.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <ctype.h>
#include <glob.h>

static struct uci_backend uci_file_backend;

/*
 * verify that the end of the line or command is reached.
 * throw an error if extra arguments are given on the command line
 */
static void assert_eol(struct uci_context *ctx, char **str)
{
	char *tmp;

	skip_whitespace(ctx, str);
	tmp = next_arg(ctx, str, false, false);
	if (*tmp && (ctx->flags & UCI_FLAG_STRICT))
		uci_parse_error(ctx, *str, "too many arguments");
}

/* 
 * switch to a different config, either triggered by uci_load, or by a
 * 'package <...>' statement in the import file
 */
static void uci_switch_config(struct uci_context *ctx)
{
	struct uci_parse_context *pctx;
	struct uci_element *e;
	const char *name;

	pctx = ctx->pctx;
	name = pctx->name;

	/* add the last config to main config file list */
	if (pctx->package) {
		pctx->package->backend = ctx->backend;
		uci_list_add(&ctx->root, &pctx->package->e.list);

		pctx->package = NULL;
		pctx->section = NULL;
	}

	if (!name)
		return;

	/* 
	 * if an older config under the same name exists, unload it
	 * ignore errors here, e.g. if the config was not found
	 */
	e = uci_lookup_list(&ctx->root, name);
	if (e)
		UCI_THROW(ctx, UCI_ERR_DUPLICATE);
	pctx->package = uci_alloc_package(ctx, name);
}

/*
 * parse the 'package' uci command (next config package)
 */
static void uci_parse_package(struct uci_context *ctx, char **str, bool single)
{
	char *name = NULL;

	/* command string null-terminated by strtok */
	*str += strlen(*str) + 1;

	name = next_arg(ctx, str, true, true);
	assert_eol(ctx, str);
	if (single)
		return;

	ctx->pctx->name = name;
	uci_switch_config(ctx);
}

/*
 * parse the 'config' uci command (open a section)
 */
static void uci_parse_config(struct uci_context *ctx, char **str)
{
	struct uci_parse_context *pctx = ctx->pctx;
	struct uci_element *e;
	struct uci_ptr ptr;
	char *name = NULL;
	char *type = NULL;

	uci_fixup_section(ctx, ctx->pctx->section);
	if (!ctx->pctx->package) {
		if (!ctx->pctx->name)
			uci_parse_error(ctx, *str, "attempting to import a file without a package name");

		uci_switch_config(ctx);
	}

	/* command string null-terminated by strtok */
	*str += strlen(*str) + 1;

	type = next_arg(ctx, str, true, false);
	if (!uci_validate_type(type))
		uci_parse_error(ctx, type, "invalid character in field");
	name = next_arg(ctx, str, false, true);
	assert_eol(ctx, str);

	if (!name) {
		ctx->internal = !pctx->merge;
		UCI_NESTED(uci_add_section, ctx, pctx->package, type, &pctx->section);
	} else {
		UCI_NESTED(uci_fill_ptr, ctx, &ptr, &pctx->package->e, false);
		e = uci_lookup_list(&pctx->package->sections, name);
		if (e)
			ptr.s = uci_to_section(e);
		ptr.section = name;
		ptr.value = type;

		ctx->internal = !pctx->merge;
		UCI_NESTED(uci_set, ctx, &ptr);
		pctx->section = uci_to_section(ptr.last);
	}
}

/*
 * parse the 'option' uci command (open a value)
 */
static void uci_parse_option(struct uci_context *ctx, char **str, bool list)
{
	struct uci_parse_context *pctx = ctx->pctx;
	struct uci_element *e;
	struct uci_ptr ptr;
	char *name = NULL;
	char *value = NULL;

	if (!pctx->section)
		uci_parse_error(ctx, *str, "option/list command found before the first section");

	/* command string null-terminated by strtok */
	*str += strlen(*str) + 1;

	name = next_arg(ctx, str, true, true);
	value = next_arg(ctx, str, false, false);
	assert_eol(ctx, str);

	UCI_NESTED(uci_fill_ptr, ctx, &ptr, &pctx->section->e, false);
	e = uci_lookup_list(&pctx->section->options, name);
	if (e)
		ptr.o = uci_to_option(e);
	ptr.option = name;
	ptr.value = value;

	ctx->internal = !pctx->merge;
	if (list)
		UCI_NESTED(uci_add_list, ctx, &ptr);
	else
		UCI_NESTED(uci_set, ctx, &ptr);
}

/*
 * parse a complete input line, split up combined commands by ';'
 */
static void uci_parse_line(struct uci_context *ctx, bool single)
{
	struct uci_parse_context *pctx = ctx->pctx;
	char *word, *brk;

	word = pctx->buf;
	do {
		brk = NULL;
		word = strtok_r(word, " \t", &brk);
		if (!word)
			return;

		switch(word[0]) {
			case 0:
			case '#':
				return;
			case 'p':
				if ((word[1] == 0) || !strcmp(word + 1, "ackage"))
					uci_parse_package(ctx, &word, single);
				else
					goto invalid;
				break;
			case 'c':
				if ((word[1] == 0) || !strcmp(word + 1, "onfig"))
					uci_parse_config(ctx, &word);
				else
					goto invalid;
				break;
			case 'o':
				if ((word[1] == 0) || !strcmp(word + 1, "ption"))
					uci_parse_option(ctx, &word, false);
				else
					goto invalid;
				break;
			case 'l':
				if ((word[1] == 0) || !strcmp(word + 1, "ist"))
					uci_parse_option(ctx, &word, true);
				else
					goto invalid;
				break;
			default:
				goto invalid;
		}
		continue;
invalid:
		uci_parse_error(ctx, word, "invalid command");
	} while (1);
}

/* max number of characters that escaping adds to the string */
#define UCI_QUOTE_ESCAPE	"'\\''"

/*
 * escape an uci string for export
 */
static char *uci_escape(struct uci_context *ctx, const char *str)
{
	const char *end;
	int ofs = 0;

	if (!ctx->buf) {
		ctx->bufsz = LINEBUF;
		ctx->buf = malloc(LINEBUF);
	}

	while (1) {
		int len;

		end = strchr(str, '\'');
		if (!end)
			end = str + strlen(str);
		len = end - str;

		/* make sure that we have enough room in the buffer */
		while (ofs + len + sizeof(UCI_QUOTE_ESCAPE) + 1 > ctx->bufsz) {
			ctx->bufsz *= 2;
			ctx->buf = uci_realloc(ctx, ctx->buf, ctx->bufsz);
		}

		/* copy the string until the character before the quote */
		memcpy(&ctx->buf[ofs], str, len);
		ofs += len;

		/* end of string? return the buffer */
		if (*end == 0)
			break;

		memcpy(&ctx->buf[ofs], UCI_QUOTE_ESCAPE, sizeof(UCI_QUOTE_ESCAPE));
		ofs += strlen(&ctx->buf[ofs]);
		str = end + 1;
	}

	ctx->buf[ofs] = 0;
	return ctx->buf;
}

/*
 * export a single config package to a file stream
 */
static void uci_export_package(struct uci_package *p, FILE *stream, bool header)
{
	struct uci_context *ctx = p->ctx;
	struct uci_element *s, *o, *i;

	if (header)
		fprintf(stream, "package '%s'\n", uci_escape(ctx, p->e.name));
	uci_foreach_element(&p->sections, s) {
		struct uci_section *sec = uci_to_section(s);
		fprintf(stream, "\nconfig '%s'", uci_escape(ctx, sec->type));
		if (!sec->anonymous || (ctx->flags & UCI_FLAG_EXPORT_NAME))
			fprintf(stream, " '%s'", uci_escape(ctx, sec->e.name));
		fprintf(stream, "\n");
		uci_foreach_element(&sec->options, o) {
			struct uci_option *opt = uci_to_option(o);
			switch(opt->type) {
			case UCI_TYPE_STRING:
				fprintf(stream, "\toption '%s'", uci_escape(ctx, opt->e.name));
				fprintf(stream, " '%s'\n", uci_escape(ctx, opt->v.string));
				break;
			case UCI_TYPE_LIST:
				uci_foreach_element(&opt->v.list, i) {
					fprintf(stream, "\tlist '%s'", uci_escape(ctx, opt->e.name));
					fprintf(stream, " '%s'\n", uci_escape(ctx, i->name));
				}
				break;
			default:
				fprintf(stream, "\t# unknown type for option '%s'\n", uci_escape(ctx, opt->e.name));
				break;
			}
		}
	}
	fprintf(stream, "\n");
}

int uci_export(struct uci_context *ctx, FILE *stream, struct uci_package *package, bool header)
{
	struct uci_element *e;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, stream != NULL);

	if (package)
		uci_export_package(package, stream, header);
	else {
		uci_foreach_element(&ctx->root, e) {
			uci_export_package(uci_to_package(e), stream, header);
		}
	}

	return 0;
}

int uci_import(struct uci_context *ctx, FILE *stream, const char *name, struct uci_package **package, bool single)
{
	struct uci_parse_context *pctx;
	UCI_HANDLE_ERR(ctx);

	/* make sure no memory from previous parse attempts is leaked */
	uci_cleanup(ctx);

	uci_alloc_parse_context(ctx);
	pctx = ctx->pctx;
	pctx->file = stream;
	if (*package && single) {
		pctx->package = *package;
		pctx->merge = true;
	}

	/*
	 * If 'name' was supplied, assume that the supplied stream does not contain
	 * the appropriate 'package <name>' string to specify the config name
	 * NB: the config file can still override the package name
	 */
	if (name) {
		UCI_ASSERT(ctx, uci_validate_package(name));
		pctx->name = name;
	}

	while (!feof(pctx->file)) {
		uci_getln(ctx, 0);
		UCI_TRAP_SAVE(ctx, error);
		if (pctx->buf[0])
			uci_parse_line(ctx, single);
		UCI_TRAP_RESTORE(ctx);
		continue;
error:
		if (ctx->flags & UCI_FLAG_PERROR)
			uci_perror(ctx, NULL);
		if ((ctx->err != UCI_ERR_PARSE) ||
			(ctx->flags & UCI_FLAG_STRICT))
			UCI_THROW(ctx, ctx->err);
	}

	uci_fixup_section(ctx, ctx->pctx->section);
	if (!pctx->package && name)
		uci_switch_config(ctx);
	if (package)
		*package = pctx->package;
	if (pctx->merge)
		pctx->package = NULL;

	pctx->name = NULL;
	uci_switch_config(ctx);

	/* no error happened, we can get rid of the parser context now */
	uci_cleanup(ctx);

	return 0;
}


static char *uci_config_path(struct uci_context *ctx, const char *name)
{
	char *filename;

	UCI_ASSERT(ctx, uci_validate_package(name));
	filename = uci_malloc(ctx, strlen(name) + strlen(ctx->confdir) + 2);
	sprintf(filename, "%s/%s", ctx->confdir, name);

	return filename;
}

void uci_file_commit(struct uci_context *ctx, struct uci_package **package, bool overwrite)
{
	struct uci_package *p = *package;
	FILE *f = NULL;
	char *name = NULL;
	char *path = NULL;

	if (!p->path) {
		if (overwrite)
			p->path = uci_config_path(ctx, p->e.name);
		else
			UCI_THROW(ctx, UCI_ERR_INVAL);
	}

	/* open the config file for writing now, so that it is locked */
	f = uci_open_stream(ctx, p->path, SEEK_SET, true, true);

	/* flush unsaved changes and reload from history file */
	UCI_TRAP_SAVE(ctx, done);
	if (p->has_history) {
		if (!overwrite) {
			name = uci_strdup(ctx, p->e.name);
			path = uci_strdup(ctx, p->path);
			/* dump our own changes to the history file */
			if (!uci_list_empty(&p->history))
				UCI_INTERNAL(uci_save, ctx, p);

			/* 
			 * other processes might have modified the config 
			 * as well. dump and reload 
			 */
			uci_free_package(&p);
			uci_cleanup(ctx);
			UCI_INTERNAL(uci_import, ctx, f, name, &p, true);

			p->path = path;
			p->has_history = true;
			*package = p;

			/* freed together with the uci_package */
			path = NULL;
		}

		/* flush history */
		if (!uci_load_history(ctx, p, true))
			goto done;
	}

	rewind(f);
	if (ftruncate(fileno(f), 0) < 0)
		UCI_THROW(ctx, UCI_ERR_IO);

	uci_export(ctx, f, p, false);
	UCI_TRAP_RESTORE(ctx);

done:
	if (name)
		free(name);
	if (path)
		free(path);
	uci_close_stream(f);
	if (ctx->err)
		UCI_THROW(ctx, ctx->err);
}


/* 
 * This function returns the filename by returning the string
 * after the last '/' character. By checking for a non-'\0'
 * character afterwards, directories are ignored (glob marks
 * those with a trailing '/'
 */
static inline char *get_filename(char *path)
{
	char *p;

	p = strrchr(path, '/');
	p++;
	if (!*p)
		return NULL;
	return p;
}

static char **uci_list_config_files(struct uci_context *ctx)
{
	char **configs;
	glob_t globbuf;
	int size, i;
	char *buf;
	char *dir;

	dir = uci_malloc(ctx, strlen(ctx->confdir) + 1 + sizeof("/*"));
	sprintf(dir, "%s/*", ctx->confdir);
	if (glob(dir, GLOB_MARK, NULL, &globbuf) != 0)
		UCI_THROW(ctx, UCI_ERR_NOTFOUND);

	size = sizeof(char *) * (globbuf.gl_pathc + 1);
	for(i = 0; i < globbuf.gl_pathc; i++) {
		char *p;

		p = get_filename(globbuf.gl_pathv[i]);
		if (!p)
			continue;

		size += strlen(p) + 1;
	}

	configs = uci_malloc(ctx, size);
	buf = (char *) &configs[globbuf.gl_pathc + 1];
	for(i = 0; i < globbuf.gl_pathc; i++) {
		char *p;

		p = get_filename(globbuf.gl_pathv[i]);
		if (!p)
			continue;

		if (!uci_validate_package(p))
			continue;

		configs[i] = buf;
		strcpy(buf, p);
		buf += strlen(buf) + 1;
	}
	free(dir);
	globfree(&globbuf);
	return configs;
}

static struct uci_package *uci_file_load(struct uci_context *ctx, const char *name)
{
	struct uci_package *package = NULL;
	char *filename;
	bool confdir;
	FILE *file = NULL;

	switch (name[0]) {
	case '.':
		/* relative path outside of /etc/config */
		if (name[1] != '/')
			UCI_THROW(ctx, UCI_ERR_NOTFOUND);
		/* fall through */
	case '/':
		/* absolute path outside of /etc/config */
		filename = uci_strdup(ctx, name);
		name = strrchr(name, '/') + 1;
		confdir = false;
		break;
	default:
		/* config in /etc/config */
		filename = uci_config_path(ctx, name);
		confdir = true;
		break;
	}

	file = uci_open_stream(ctx, filename, SEEK_SET, false, false);
	ctx->err = 0;
	UCI_TRAP_SAVE(ctx, done);
	UCI_INTERNAL(uci_import, ctx, file, name, &package, true);
	UCI_TRAP_RESTORE(ctx);

	if (package) {
		package->path = filename;
		package->has_history = confdir;
		uci_load_history(ctx, package, false);
	}

done:
	uci_close_stream(file);
	if (ctx->err)
		UCI_THROW(ctx, ctx->err);
	return package;
}

static UCI_BACKEND(uci_file_backend, "file",
	.load = uci_file_load,
	.commit = uci_file_commit,
	.list_configs = uci_list_config_files,
);
