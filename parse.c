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

#include <ctype.h>

#define LINEBUF	128
#define LINEBUF_MAX	4096

/*
 * Fetch a new line from the input stream and resize buffer if necessary
 */
static void uci_getln(struct uci_context *ctx)
{
	struct uci_parse_context *pctx = ctx->pctx;
	char *p;
	int ofs;

	if (pctx->buf == NULL) {
		pctx->buf = uci_malloc(ctx, LINEBUF);
		pctx->bufsz = LINEBUF;
	}

	ofs = 0;
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

	if (pctx->cfg) {
		uci_list_del(&pctx->cfg->list);
		uci_drop_file(pctx->cfg);
	}
	if (pctx->buf)
		free(pctx->buf);
	if (pctx->file)
		fclose(pctx->file);

	free(pctx);
}

/*
 * move the string pointer forward until a non-whitespace character or
 * EOL is reached
 */
static void skip_whitespace(char **str)
{
	while (**str && isspace(**str))
		*str += 1;
}

/*
 * parse a double quoted string argument from the command line
 */
static char *parse_double_quote(char **str)
{
	char *val;

	*str += 1;
	val = *str;
	while (**str) {

		/* skip escaped characters */
		if (**str == '\\') {
			*str += 2;
			continue;
		}

		/* check for the end of the quoted string */
		if (**str == '"') {
			**str = 0;
			*str += 1;
			return val;
		}
		*str += 1;
	}

	return NULL;
}

/*
 * parse a single quoted string argument from the command line
 */
static char *parse_single_quote(char **str)
{
	/* TODO: implement */
	return NULL;
}

/*
 * extract the next word from the command line (unquoted argument)
 */
static char *parse_unquoted(char **str)
{
	char *val;

	val = *str;

	while (**str && !isspace(**str))
		*str += 1;

	if (**str) {
		**str = 0;
		*str += 1;
	}

	return val;
}

/*
 * extract the next argument from the command line
 */
static char *next_arg(struct uci_context *ctx, char **str, bool required)
{
	char *val;

	skip_whitespace(str);
	switch (**str) {
		case '"':
			val = parse_double_quote(str);
			break;
		case '\'':
			val = parse_single_quote(str);
			break;
		case 0:
			val = NULL;
			break;
		default:
			val = parse_unquoted(str);
			break;
	}

	if (required && !val) {
		ctx->pctx->byte = *str - ctx->pctx->buf;
		UCI_THROW(ctx, UCI_ERR_PARSE);
	}

	return val;
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
		ctx->pctx->byte = tmp - ctx->pctx->buf;
		UCI_THROW(ctx, UCI_ERR_PARSE);
	}
}

/*
 * parse the 'config' uci command (open a section)
 */
static void uci_parse_config(struct uci_context *ctx, char **str)
{
	char *type, *name;

	*str += strlen(*str) + 1;

	if (!*str) {
		ctx->pctx->byte = *str - ctx->pctx->buf;
		UCI_THROW(ctx, UCI_ERR_PARSE);
	}

	type = next_arg(ctx, str, true);
	name = next_arg(ctx, str, false);
	assert_eol(ctx, str);

	DPRINTF("Section<%s>: %s\n", type, name);
}

/*
 * parse the 'option' uci command (open a value)
 */
static void uci_parse_option(struct uci_context *ctx, char **str)
{
	char *name, *value;

	*str += strlen(*str) + 1;

	name = next_arg(ctx, str, true);
	value = next_arg(ctx, str, true);
	assert_eol(ctx, str);

	DPRINTF("\tOption: %s=\"%s\"\n", name, value);
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
			case 'c':
				if ((word[1] == 0) || !strcmp(word + 1, "onfig"))
					uci_parse_config(ctx, &word);
				break;
			case 'o':
				if ((word[1] == 0) || !strcmp(word + 1, "ption"))
					uci_parse_option(ctx, &word);
				break;
			default:
				pctx->byte = word - pctx->buf;
				UCI_THROW(ctx, UCI_ERR_PARSE);
				break;
		}
	}
}

int uci_parse(struct uci_context *ctx, const char *name)
{
	struct uci_parse_context *pctx;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, name != NULL);

	/* make sure no memory from previous parse attempts is leaked */
	uci_parse_cleanup(ctx);

	pctx = (struct uci_parse_context *) uci_malloc(ctx, sizeof(struct uci_parse_context));
	ctx->pctx = pctx;

	/* TODO: use /etc/config/ */
	pctx->file = fopen(name, "r");
	if (!pctx->file)
		UCI_THROW(ctx, UCI_ERR_NOTFOUND);

	pctx->cfg = uci_alloc_file(ctx, name);

	while (!feof(pctx->file)) {
		uci_getln(ctx);
		if (*(pctx->buf))
			uci_parse_line(ctx);
	}

	/* add to main config file list */
	uci_list_add(&ctx->root, &pctx->cfg->list);
	pctx->cfg = NULL;

	/* if no error happened, we can get rid of the parser context now */
	uci_parse_cleanup(ctx);

	return 0;
}


