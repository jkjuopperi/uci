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
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <ctype.h>

#define LINEBUF	32
#define LINEBUF_MAX	4096

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
		uci_free_package(pctx->package);

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
	if (required && !*val)
		uci_parse_error(ctx, *str, "insufficient arguments");
	if (name && !uci_validate_name(val))
		uci_parse_error(ctx, val, "invalid character in field");

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
	if (tmp && *tmp)
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

/*
 * parse the 'config' uci command (open a section)
 */
static void uci_parse_config(struct uci_context *ctx, char **str)
{
	char *name = NULL;
	char *type = NULL;

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
	ctx->pctx->section = uci_alloc_section(ctx->pctx->package, type, name);
}

/*
 * parse the 'option' uci command (open a value)
 */
static void uci_parse_option(struct uci_context *ctx, char **str)
{
	char *name = NULL;
	char *value = NULL;

	if (!ctx->pctx->section)
		uci_parse_error(ctx, *str, "option command found before the first section");

	/* command string null-terminated by strtok */
	*str += strlen(*str) + 1;

	name = next_arg(ctx, str, true, true);
	value = next_arg(ctx, str, true, false);
	assert_eol(ctx, str);
	uci_alloc_option(ctx->pctx->section, name, value);
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

		switch(word[0]) {
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
		fprintf(stream, " '%s'\n", uci_escape(ctx, sec->e.name));
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

	/* make sure no memory from previous parse attempts is leaked */
	uci_file_cleanup(ctx);

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

	if (package)
		*package = pctx->package;

	pctx->name = NULL;
	uci_switch_config(ctx);

	/* no error happened, we can get rid of the parser context now */
	uci_file_cleanup(ctx);

	return 0;
}

/*
 * open a stream and go to the right position
 *
 * note: when opening for write and seeking to the beginning of
 * the stream, truncate the file
 */
static FILE *uci_open_stream(struct uci_context *ctx, const char *filename, int pos, bool write)
{
	struct stat statbuf;
	FILE *file = NULL;
	int fd, ret;

	if (!write && ((stat(filename, &statbuf) < 0) ||
		((statbuf.st_mode &  S_IFMT) != S_IFREG))) {
		UCI_THROW(ctx, UCI_ERR_NOTFOUND);
	}

	fd = open(filename, (write ? O_RDWR | O_CREAT : O_RDONLY));
	if (fd <= 0)
		goto error;

	if (flock(fd, (write ? LOCK_EX : LOCK_SH)) < 0)
		goto error;

	if (write && (pos == SEEK_SET))
		ret = ftruncate(fd, 0);
	else
		ret = lseek(fd, 0, pos);

	if (ret < 0)
		goto error;

	file = fdopen(fd, (write ? "w" : "r"));
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

static void uci_parse_history_line(struct uci_context *ctx, struct uci_package *p, char *buf)
{
	bool delete = false;
	char *package = NULL;
	char *section = NULL;
	char *option = NULL;
	char *value = NULL;

	if (buf[0] == '-') {
		delete = true;
		buf++;
	}

	UCI_INTERNAL(uci_parse_tuple, ctx, buf, &package, &section, &option, &value);
	if (!package || !section || (!delete && !value))
		goto error;
	if (strcmp(package, p->e.name) != 0)
		goto error;
	if (!uci_validate_name(section))
		goto error;
	if (option && !uci_validate_name(option))
		goto error;

	if (delete)
		UCI_INTERNAL(uci_del, ctx, p, section, option);
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
	uci_file_cleanup(ctx);

	pctx = (struct uci_parse_context *) uci_malloc(ctx, sizeof(struct uci_parse_context));
	ctx->pctx = pctx;
	pctx->file = stream;

	rewind(stream);
	while (!feof(pctx->file)) {
		uci_getln(ctx, 0);
		if (!pctx->buf[0])
			continue;
		uci_parse_history_line(ctx, p, pctx->buf);
	}

	/* no error happened, we can get rid of the parser context now */
	uci_file_cleanup(ctx);
}

static void uci_load_history(struct uci_context *ctx, struct uci_package *p)
{
	char *filename = NULL;
	FILE *f = NULL;

	if (!p->confdir)
		return;
	if ((asprintf(&filename, "%s/%s", UCI_SAVEDIR, p->e.name) < 0) || !filename)
		UCI_THROW(ctx, UCI_ERR_MEM);

	UCI_TRAP_SAVE(ctx, done);
	f = uci_open_stream(ctx, filename, SEEK_SET, false);
	uci_parse_history(ctx, f, p);
	UCI_TRAP_RESTORE(ctx);
done:
	if (filename)
		free(filename);
	uci_close_stream(f);
	ctx->errno = 0;
}

int uci_load(struct uci_context *ctx, const char *name, struct uci_package **package)
{
	char *filename;
	bool confdir;
	FILE *file = NULL;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, name != NULL);

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
		if (strchr(name, '/'))
			UCI_THROW(ctx, UCI_ERR_INVAL);
		filename = uci_malloc(ctx, strlen(name) + sizeof(UCI_CONFDIR) + 2);
		sprintf(filename, UCI_CONFDIR "/%s", name);
		confdir = true;
		break;
	}

	file = uci_open_stream(ctx, filename, SEEK_SET, false);
	ctx->errno = 0;
	UCI_TRAP_SAVE(ctx, done);
	uci_import(ctx, file, name, package, true);
	UCI_TRAP_RESTORE(ctx);

	if (*package) {
		(*package)->path = filename;
		(*package)->confdir = confdir;
		uci_load_history(ctx, *package);
	}

done:
	uci_close_stream(file);
	return ctx->errno;
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
	 * directly
	 */
	if (!p->confdir)
		return uci_commit(ctx, p);

	if (uci_list_empty(&p->history))
		return 0;

	if ((asprintf(&filename, "%s/%s", UCI_SAVEDIR, p->e.name) < 0) || !filename)
		UCI_THROW(ctx, UCI_ERR_MEM);

	ctx->errno = 0;
	UCI_TRAP_SAVE(ctx, done);
	f = uci_open_stream(ctx, filename, SEEK_END, true);
	UCI_TRAP_RESTORE(ctx);

	uci_foreach_element_safe(&p->history, tmp, e) {
		struct uci_history *h = uci_to_history(e);

		if (h->cmd == UCI_CMD_REMOVE)
			fprintf(f, "-");

		fprintf(f, "%s.%s", p->e.name, h->section);
		if (e->name)
			fprintf(f, ".%s", e->name);

		if (h->cmd == UCI_CMD_REMOVE)
			fprintf(f, "\n");
		else
			fprintf(f, "=%s\n", h->value);
		uci_list_del(&e->list);
	}

done:
	uci_close_stream(f);
	if (filename)
		free(filename);
	if (ctx->errno)
		UCI_THROW(ctx, ctx->errno);

	return 0;
}

int uci_commit(struct uci_context *ctx, struct uci_package *p)
{
	FILE *f = NULL;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, p != NULL);
	UCI_ASSERT(ctx, p->path != NULL);

	f = uci_open_stream(ctx, p->path, SEEK_SET, true);

	UCI_TRAP_SAVE(ctx, done);
	uci_export(ctx, f, p, false);
	UCI_TRAP_RESTORE(ctx);

done:
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

	UCI_HANDLE_ERR(ctx);

	if (glob(UCI_CONFDIR "/*", GLOB_MARK, NULL, &globbuf) != 0)
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

	return 0;
}

