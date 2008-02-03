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

/*
 * Clean up all extra memory used by the parser and exporter
 */
static void uci_file_cleanup(struct uci_context *ctx)
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


/* 
 * parse a character escaped by '\'
 * returns true if the escaped character is to be parsed
 * returns false if the escaped character is to be ignored
 */
static inline bool parse_backslash(struct uci_context *ctx, char **str)
{
	/* skip backslash */
	*str += 1;

	/* undecoded backslash at the end of line, fetch the next line */
	if (!**str) {
		*str += 1;
		uci_getln(ctx, *str - ctx->pctx->buf);
		return false;
	}

	/* FIXME: decode escaped char, necessary? */
	return true;
}

/*
 * move the string pointer forward until a non-whitespace character or
 * EOL is reached
 */
static void skip_whitespace(struct uci_context *ctx, char **str)
{
restart:
	while (**str && isspace(**str))
		*str += 1;

	if (**str == '\\') {
		if (!parse_backslash(ctx, str))
			goto restart;
	}
}

static inline void addc(char **dest, char **src)
{
	**dest = **src;
	*dest += 1;
	*src += 1;
}

/*
 * parse a double quoted string argument from the command line
 */
static void parse_double_quote(struct uci_context *ctx, char **str, char **target)
{
	char c;

	/* skip quote character */
	*str += 1;

	while ((c = **str)) {
		switch(c) {
		case '"':
			**target = 0;
			*str += 1;
			return;
		case '\\':
			if (!parse_backslash(ctx, str))
				continue;
			/* fall through */
		default:
			addc(target, str);
			break;
		}
	}
	uci_parse_error(ctx, *str, "unterminated \"");
}

/*
 * parse a single quoted string argument from the command line
 */
static void parse_single_quote(struct uci_context *ctx, char **str, char **target)
{
	char c;
	/* skip quote character */
	*str += 1;

	while ((c = **str)) {
		switch(c) {
		case '\'':
			**target = 0;
			*str += 1;
			return;
		default:
			addc(target, str);
		}
	}
	uci_parse_error(ctx, *str, "unterminated '");
}

/*
 * parse a string from the command line and detect the quoting style
 */
static void parse_str(struct uci_context *ctx, char **str, char **target)
{
	do {
		switch(**str) {
		case '\'':
			parse_single_quote(ctx, str, target);
			break;
		case '"':
			parse_double_quote(ctx, str, target);
			break;
		case '#':
			**str = 0;
			/* fall through */
		case 0:
			goto done;
		case '\\':
			if (!parse_backslash(ctx, str))
				continue;
			/* fall through */
		default:
			addc(target, str);
			break;
		}
	} while (**str && !isspace(**str));
done:

	/* 
	 * if the string was unquoted and we've stopped at a whitespace
	 * character, skip to the next one, because the whitespace will
	 * be overwritten by a null byte here
	 */
	if (**str)
		*str += 1;

	/* terminate the parsed string */
	**target = 0;
}

/*
 * extract the next argument from the command line
 */
static char *next_arg(struct uci_context *ctx, char **str, bool required, bool name)
{
	char *val;
	char *ptr;

	val = ptr = *str;
	skip_whitespace(ctx, str);
	parse_str(ctx, str, &ptr);
	if (!*val) {
		if (required)
			uci_parse_error(ctx, *str, "insufficient arguments");
		goto done;
	}

	if (name && !uci_validate_name(val))
		uci_parse_error(ctx, val, "invalid character in field");

done:
	return val;
}

/*
 * verify that the end of the line or command is reached.
 * throw an error if extra arguments are given on the command line
 */
static void assert_eol(struct uci_context *ctx, char **str)
{
	char *tmp;

	tmp = next_arg(ctx, str, false, false);
	if (tmp && *tmp && (ctx->flags & UCI_FLAG_STRICT))
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
	e = uci_lookup_list(ctx, &ctx->root, name);
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

/* Based on an efficient hash function published by D. J. Bernstein */
static unsigned int djbhash(unsigned int hash, char *str)
{
	int len = strlen(str);
	int i;

	/* initial value */
	if (hash == ~0)
		hash = 5381;

	for(i = 0; i < len; i++) {
		hash = ((hash << 5) + hash) + str[i];
	}
	return (hash & 0x7FFFFFFF);
}

/* fix up an unnamed section */
static void uci_fixup_section(struct uci_context *ctx, struct uci_section *s)
{
	unsigned int hash = ~0;
	struct uci_element *e;
	char buf[16];

	if (!s || s->e.name)
		return;

	/*
	 * Generate a name for unnamed sections. This is used as reference
	 * when locating or updating the section from apps/scripts.
	 * To make multiple concurrent versions somewhat safe for updating,
	 * the name is generated from a hash of its type and name/value
	 * pairs of its option, and it is prefixed by a counter value.
	 * If the order of the unnamed sections changes for some reason,
	 * updates to them will be rejected.
	 */
	hash = djbhash(hash, s->type);
	uci_foreach_element(&s->options, e) {
		hash = djbhash(hash, e->name);
		hash = djbhash(hash, uci_to_option(e)->value);
	}
	sprintf(buf, "cfg%02x%04x", ++s->package->n_section, hash % (1 << 16));
	s->e.name = uci_strdup(ctx, buf);
}

/*
 * parse the 'config' uci command (open a section)
 */
static void uci_parse_config(struct uci_context *ctx, char **str)
{
	struct uci_parse_context *pctx = ctx->pctx;
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

	type = next_arg(ctx, str, true, true);
	name = next_arg(ctx, str, false, true);
	assert_eol(ctx, str);

	if (pctx->merge) {
		UCI_TRAP_SAVE(ctx, error);
		uci_set(ctx, pctx->package, name, NULL, type);
		UCI_TRAP_RESTORE(ctx);
		return;
error:
		UCI_THROW(ctx, ctx->errno);
	} else
		pctx->section = uci_alloc_section(pctx->package, type, name);
}

/*
 * parse the 'option' uci command (open a value)
 */
static void uci_parse_option(struct uci_context *ctx, char **str)
{
	struct uci_parse_context *pctx = ctx->pctx;
	char *name = NULL;
	char *value = NULL;

	if (!pctx->section)
		uci_parse_error(ctx, *str, "option command found before the first section");

	/* command string null-terminated by strtok */
	*str += strlen(*str) + 1;

	name = next_arg(ctx, str, true, true);
	value = next_arg(ctx, str, true, false);
	assert_eol(ctx, str);

	if (pctx->merge) {
		UCI_TRAP_SAVE(ctx, error);
		uci_set(ctx, pctx->package, pctx->section->e.name, name, value);
		UCI_TRAP_RESTORE(ctx);
		return;
error:
		UCI_THROW(ctx, ctx->errno);
	} else
		uci_alloc_option(pctx->section, name, value);
}


/*
 * parse a complete input line, split up combined commands by ';'
 */
static void uci_parse_line(struct uci_context *ctx, bool single)
{
	struct uci_parse_context *pctx = ctx->pctx;
	char *word, *brk = NULL;

	for (word = strtok_r(pctx->buf, ";", &brk);
		 word;
		 word = strtok_r(NULL, ";", &brk)) {

		char *pbrk = NULL;
		word = strtok_r(word, " \t", &pbrk);

		if (!word)
			continue;

		switch(word[0]) {
			case '#':
				return;
			case 'p':
				if ((word[1] == 0) || !strcmp(word + 1, "ackage"))
					uci_parse_package(ctx, &word, single);
				break;
			case 'c':
				if ((word[1] == 0) || !strcmp(word + 1, "onfig"))
					uci_parse_config(ctx, &word);
				break;
			case 'o':
				if ((word[1] == 0) || !strcmp(word + 1, "ption"))
					uci_parse_option(ctx, &word);
				break;
			default:
				uci_parse_error(ctx, word, "unterminated command");
				break;
		}
	}
}

/* max number of characters that escaping adds to the string */
#define UCI_QUOTE_ESCAPE	"'\\''"

/*
 * escape an uci string for export
 */
static char *uci_escape(struct uci_context *ctx, char *str)
{
	char *s, *p;
	int pos = 0;

	if (!ctx->buf) {
		ctx->bufsz = LINEBUF;
		ctx->buf = malloc(LINEBUF);
	}

	s = str;
	p = strchr(str, '\'');
	if (!p)
		return str;

	do {
		int len = p - s;
		if (len > 0) {
			if (p + sizeof(UCI_QUOTE_ESCAPE) - str >= ctx->bufsz) {
				ctx->bufsz *= 2;
				ctx->buf = realloc(ctx->buf, ctx->bufsz);
				if (!ctx->buf)
					UCI_THROW(ctx, UCI_ERR_MEM);
			}
			memcpy(&ctx->buf[pos], s, len);
			pos += len;
		}
		strcpy(&ctx->buf[pos], UCI_QUOTE_ESCAPE);
		pos += sizeof(UCI_QUOTE_ESCAPE);
		s = p + 1;
	} while ((p = strchr(s, '\'')));

	return ctx->buf;
}


/*
 * export a single config package to a file stream
 */
static void uci_export_package(struct uci_package *p, FILE *stream, bool header)
{
	struct uci_context *ctx = p->ctx;
	struct uci_element *s, *o;

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
			fprintf(stream, "\toption '%s'", uci_escape(ctx, opt->e.name));
			fprintf(stream, " '%s'\n", uci_escape(ctx, opt->value));
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
	uci_file_cleanup(ctx);

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
		UCI_ASSERT(ctx, uci_validate_name(name));
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
		if ((ctx->errno != UCI_ERR_PARSE) ||
			(ctx->flags & UCI_FLAG_STRICT))
			UCI_THROW(ctx, ctx->errno);
	}

	uci_fixup_section(ctx, ctx->pctx->section);
	if (package)
		*package = pctx->package;
	if (pctx->merge)
		pctx->package = NULL;

	pctx->name = NULL;
	uci_switch_config(ctx);

	/* no error happened, we can get rid of the parser context now */
	uci_file_cleanup(ctx);

	return 0;
}


static char *uci_config_path(struct uci_context *ctx, const char *name)
{
	char *filename;

	UCI_ASSERT(ctx, uci_validate_name(name));
	filename = uci_malloc(ctx, strlen(name) + strlen(ctx->confdir) + 2);
	sprintf(filename, "%s/%s", ctx->confdir, name);

	return filename;
}

int uci_load(struct uci_context *ctx, const char *name, struct uci_package **package)
{
	char *filename;
	bool confdir;
	FILE *file = NULL;

	UCI_HANDLE_ERR(ctx);

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
	ctx->errno = 0;
	UCI_TRAP_SAVE(ctx, done);
	UCI_INTERNAL(uci_import, ctx, file, name, package, true);
	UCI_TRAP_RESTORE(ctx);

	if (*package) {
		(*package)->path = filename;
		(*package)->confdir = confdir;
		uci_load_history(ctx, *package, false);
	}

done:
	uci_close_stream(file);
	return ctx->errno;
}

int uci_commit(struct uci_context *ctx, struct uci_package **package, bool overwrite)
{
	struct uci_package *p;
	FILE *f = NULL;
	char *name = NULL;
	char *path = NULL;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, package != NULL);
	p = *package;

	UCI_ASSERT(ctx, p != NULL);
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
	if (p->confdir) {
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
			uci_file_cleanup(ctx);
			UCI_INTERNAL(uci_import, ctx, f, name, &p, true);

			p->path = path;
			p->confdir = true;
			*package = p;

			/* freed together with the uci_package */
			path = NULL;

			/* check for updated history, flush */
			uci_load_history(ctx, p, true);
		} else {
			/* flush history */
			uci_load_history(ctx, NULL, true);
		}
	}

	rewind(f);
	ftruncate(fileno(f), 0);

	uci_export(ctx, f, p, false);
	UCI_TRAP_RESTORE(ctx);

done:
	if (name)
		free(name);
	if (path)
		free(path);
	uci_close_stream(f);
	if (ctx->errno)
		UCI_THROW(ctx, ctx->errno);

	return 0;
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

int uci_list_configs(struct uci_context *ctx, char ***list)
{
	char **configs;
	glob_t globbuf;
	int size, i;
	char *buf;
	char *dir;

	UCI_HANDLE_ERR(ctx);

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

		configs[i] = buf;
		strcpy(buf, p);
		buf += strlen(buf) + 1;
	}
	*list = configs;
	free(dir);

	return 0;
}

