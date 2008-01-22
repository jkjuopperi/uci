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

/*
 * This file contains the code for parsing uci config files
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>

#define LINEBUF	32
#define LINEBUF_MAX	4096

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
		if (!p || !p[ofs])
			return;

		ofs += strlen(p);
		if (pctx->buf[ofs - 1] == '\n') {
			pctx->line++;
			pctx->buf[ofs - 1] = 0;
			return;
		}

		if (pctx->bufsz > LINEBUF_MAX/2) {
			pctx->reason = "line too long";
			pctx->byte = LINEBUF_MAX;
			UCI_THROW(ctx, UCI_ERR_PARSE);
		}

		pctx->bufsz *= 2;
		pctx->buf = uci_realloc(ctx, pctx->buf, pctx->bufsz);
	} while (1);
}

/*
 * Clean up all extra memory used by the parser
 */
static void uci_parse_cleanup(struct uci_context *ctx)
{
	struct uci_parse_context *pctx;

	pctx = ctx->pctx;
	if (!pctx)
		return;

	ctx->pctx = NULL;
	if (pctx->cfg) {
		uci_list_del(&pctx->cfg->list);
		uci_drop_config(pctx->cfg);
	}
	if (pctx->buf)
		free(pctx->buf);
	if (pctx->file)
		fclose(pctx->file);

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
	ctx->pctx->reason = "unterminated \"";
	ctx->pctx->byte = *str - ctx->pctx->buf;
	UCI_THROW(ctx, UCI_ERR_PARSE);
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
	ctx->pctx->reason = "unterminated '";
	ctx->pctx->byte = *str - ctx->pctx->buf;
	UCI_THROW(ctx, UCI_ERR_PARSE);
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
static char *next_arg(struct uci_context *ctx, char **str, bool required)
{
	char *val;
	char *ptr;

	val = ptr = *str;
	skip_whitespace(ctx, str);
	parse_str(ctx, str, &ptr);
	if (required && !*val) {
		ctx->pctx->reason = "insufficient arguments";
		ctx->pctx->byte = *str - ctx->pctx->buf;
		UCI_THROW(ctx, UCI_ERR_PARSE);
	}

	return uci_strdup(ctx, val);
}

/*
 * verify that the end of the line or command is reached.
 * throw an error if extra arguments are given on the command line
 */
static void assert_eol(struct uci_context *ctx, char **str)
{
	char *tmp;

	tmp = next_arg(ctx, str, false);
	if (tmp && *tmp) {
		ctx->pctx->reason = "too many arguments";
		ctx->pctx->byte = tmp - ctx->pctx->buf;
		UCI_THROW(ctx, UCI_ERR_PARSE);
	}
}

/* 
 * switch to a different config, either triggered by uci_load, or by a
 * 'package <...>' statement in the import file
 */
static void uci_switch_config(struct uci_context *ctx)
{
	struct uci_parse_context *pctx;
	const char *name;

	pctx = ctx->pctx;
	name = pctx->name;

	/* add the last config to main config file list */
	if (pctx->cfg) {
		uci_list_add(&ctx->root, &pctx->cfg->list);

		pctx->cfg = NULL;
		pctx->section = NULL;
	}

	if (!name)
		return;

	/* 
	 * if an older config under the same name exists, unload it
	 * ignore errors here, e.g. if the config was not found
	 */
	UCI_TRAP_SAVE(ctx, ignore);
	uci_unload(ctx, name);
	UCI_TRAP_RESTORE(ctx);
ignore:
	ctx->errno = 0;

	pctx->cfg = uci_alloc_config(ctx, name);
}

/*
 * parse the 'package' uci command (next config package)
 */
static void uci_parse_package(struct uci_context *ctx, char **str)
{
	char *name = NULL;

	/* command string null-terminated by strtok */
	*str += strlen(*str) + 1;

	UCI_TRAP_SAVE(ctx, error);
	name = next_arg(ctx, str, true);
	assert_eol(ctx, str);
	ctx->pctx->name = name;
	uci_switch_config(ctx);
	UCI_TRAP_RESTORE(ctx);
	return;

error:
	if (name)
		free(name);
	UCI_THROW(ctx, ctx->errno);
}

/*
 * parse the 'config' uci command (open a section)
 */
static void uci_parse_config(struct uci_context *ctx, char **str)
{
	char *name = NULL;
	char *type = NULL;

	if (!ctx->pctx->cfg) {
		if (!ctx->pctx->name) {
			ctx->pctx->byte = *str - ctx->pctx->buf;
			ctx->pctx->reason = "attempting to import a file without a package name";
			UCI_THROW(ctx, UCI_ERR_PARSE);
		}
		uci_switch_config(ctx);
	}

	/* command string null-terminated by strtok */
	*str += strlen(*str) + 1;

	UCI_TRAP_SAVE(ctx, error);
	type = next_arg(ctx, str, true);
	name = next_arg(ctx, str, false);
	assert_eol(ctx, str);
	ctx->pctx->section = uci_add_section(ctx->pctx->cfg, type, name);
	UCI_TRAP_RESTORE(ctx);
	return;

error:
	if (name)
		free(name);
	if (type)
		free(type);
	UCI_THROW(ctx, ctx->errno);
}

/*
 * parse the 'option' uci command (open a value)
 */
static void uci_parse_option(struct uci_context *ctx, char **str)
{
	char *name = NULL;
	char *value = NULL;

	if (!ctx->pctx->section) {
		ctx->pctx->byte = *str - ctx->pctx->buf;
		ctx->pctx->reason = "option command found before the first section";
		UCI_THROW(ctx, UCI_ERR_PARSE);
	}
	/* command string null-terminated by strtok */
	*str += strlen(*str) + 1;

	UCI_TRAP_SAVE(ctx, error);
	name = next_arg(ctx, str, true);
	value = next_arg(ctx, str, true);
	assert_eol(ctx, str);
	uci_add_option(ctx->pctx->section, name, value);
	UCI_TRAP_RESTORE(ctx);
	return;

error:
	if (name)
		free(name);
	if (value)
		free(value);
	UCI_THROW(ctx, ctx->errno);
}


/*
 * parse a complete input line, split up combined commands by ';'
 */
static void uci_parse_line(struct uci_context *ctx)
{
	struct uci_parse_context *pctx = ctx->pctx;
	char *word, *brk;

	for (word = strtok_r(pctx->buf, ";", &brk);
		 word;
		 word = strtok_r(NULL, ";", &brk)) {

		char *pbrk;
		word = strtok_r(word, " \t", &pbrk);

		switch(word[0]) {
			case 'p':
				if ((word[1] == 0) || !strcmp(word + 1, "ackage"))
					uci_parse_package(ctx, &word);
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
				pctx->reason = "unterminated command";
				pctx->byte = word - pctx->buf;
				UCI_THROW(ctx, UCI_ERR_PARSE);
				break;
		}
	}
}

int uci_import(struct uci_context *ctx, FILE *stream, const char *name, struct uci_config **cfg)
{
	struct uci_parse_context *pctx;

	/* make sure no memory from previous parse attempts is leaked */
	uci_parse_cleanup(ctx);

	pctx = (struct uci_parse_context *) uci_malloc(ctx, sizeof(struct uci_parse_context));
	ctx->pctx = pctx;
	pctx->file = stream;

	/*
	 * If 'name' was supplied, assume that the supplied stream does not contain
	 * the appropriate 'package <name>' string to specify the config name
	 * NB: the config file can still override the package name
	 */
	if (name)
		pctx->name = name;

	while (!feof(pctx->file)) {
		uci_getln(ctx, 0);
		if (pctx->buf[0])
			uci_parse_line(ctx);
	}

	if (cfg)
		*cfg = pctx->cfg;

	pctx->name = NULL;
	uci_switch_config(ctx);

	/* no error happened, we can get rid of the parser context now */
	uci_parse_cleanup(ctx);

	return 0;
}

int uci_load(struct uci_context *ctx, const char *name, struct uci_config **cfg)
{
	struct stat statbuf;
	char *filename;
	bool confpath;
	FILE *file;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, name != NULL);

ignore:
	ctx->errno = 0;

	switch (name[0]) {
	case '.':
	case '/':
		/* absolute/relative path outside of /etc/config */
		filename = (char *) name;
		confpath = false;
		break;
	default:
		filename = uci_malloc(ctx, strlen(name) + sizeof(UCI_CONFDIR) + 2);
		sprintf(filename, UCI_CONFDIR "/%s", name);
		confpath = true;
		break;
	}

	if ((stat(filename, &statbuf) < 0) ||
		((statbuf.st_mode &  S_IFMT) != S_IFREG)) {
		UCI_THROW(ctx, UCI_ERR_NOTFOUND);
	}

	file = fopen(filename, "r");
	if (filename != name)
		free(filename);

	if (!file)
		UCI_THROW(ctx, UCI_ERR_IO);

	return uci_import(ctx, file, name, cfg);
}

