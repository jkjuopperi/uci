/*
 * Copyright (C) 2008 Felix Fietkau <nbd@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include "uci.h"

static const char *appname = "uci";

static struct uci_context *ctx;
enum {
	CMD_GET,
	CMD_SET,
	CMD_DEL
};

static void uci_usage(int argc, char **argv)
{
	fprintf(stderr,
		"Usage: %s [<options>] <command> [<arguments>]\n\n"
		"Commands:\n"
		"\texport   [<config>]\n"
		"\tshow     [<config>[.<section>[.<option>]]]\n"
		"\tget      <config>.<section>[.<option>]\n"
		"\tset      <config>.<section>[.<option>]=<value>\n"
		"\n"
		"Options:\n"
		"\t-s       force strict mode (stop on parser errors)\n"
		"\t-S       disable strict mode\n"
		"\n",
		argv[0]
	);
	exit(255);
}

static void uci_show_section(struct uci_section *p)
{
	struct uci_element *e;
	const char *cname, *sname;

	cname = p->package->e.name;
	sname = p->e.name;
	printf("%s.%s=%s\n", cname, sname, p->type);
	uci_foreach_element(&p->options, e) {
		printf("%s.%s.%s=%s\n", cname, sname, e->name, uci_to_option(e)->value);
	}
}

static void uci_show_package(struct uci_package *p, char *section)
{
	struct uci_element *e;

	uci_foreach_element( &p->sections, e) {
		if (!section || !strcmp(e->name, section))
			uci_show_section(uci_to_section(e));
	}
}

static int uci_show(int argc, char **argv)
{
	char *section = (argc > 2 ? argv[2] : NULL);
	struct uci_package *package;
	char **configs = NULL;
	char **p;

	if ((uci_list_configs(ctx, &configs) != UCI_OK) || !configs) {
		uci_perror(ctx, appname);
		return 1;
	}

	if (argc >= 2) {
		if (uci_load(ctx, argv[1], &package) != UCI_OK) {
			uci_perror(ctx, appname);
			return 1;
		}
		uci_show_package(package, section);
		uci_unload(ctx, package);
		return 0;
	}

	for (p = configs; *p; p++) {
		if ((argc < 2) || !strcmp(argv[1], *p)) {
			if (uci_load(ctx, *p, &package) != UCI_OK) {
				uci_perror(ctx, appname);
				return 1;
			}
			uci_show_package(package, section);
			uci_unload(ctx, package);
		}
	}

	return 0;
}

static int uci_do_export(int argc, char **argv)
{
	char **configs = NULL;
	char **p;

	if ((uci_list_configs(ctx, &configs) != UCI_OK) || !configs) {
		uci_perror(ctx, appname);
		return 1;
	}

	for (p = configs; *p; p++) {
		if ((argc < 2) || !strcmp(argv[1], *p)) {
			struct uci_package *package = NULL;
			int ret;

			ret = uci_load(ctx, *p, &package);
			if (ret)
				continue;
			uci_export(ctx, stdout, package, true);
			uci_unload(ctx, package);
		}
	}
	return 0;
}

static int uci_do_cmd(int cmd, int argc, char **argv)
{
	char *package = NULL;
	char *section = NULL;
	char *option = NULL;
	char *value = NULL;
	struct uci_package *p = NULL;
	struct uci_element *e = NULL;

	if (argc != 2)
		return 255;

	if (uci_parse_tuple(ctx, argv[1], &package, &section, &option, (cmd == CMD_SET ? &value : NULL)) != UCI_OK)
		return 1;

	if (uci_load(ctx, package, &p) != UCI_OK) {
		uci_perror(ctx, appname);
		return 1;
	}

	if (uci_lookup(ctx, &e, p, section, option) != UCI_OK)
		return 1;

	switch(cmd) {
	case CMD_GET:
		switch(e->type) {
		case UCI_TYPE_SECTION:
			value = uci_to_section(e)->type;
			break;
		case UCI_TYPE_OPTION:
			value = uci_to_option(e)->value;
			break;
		default:
			/* should not happen */
			return 1;
		}
		/* throw the value to stdout */
		printf("%s\n", value);
		break;
	case CMD_SET:
		if (uci_set(ctx, p, section, option, value) != UCI_OK) {
			uci_perror(ctx, appname);
			return 1;
		}
		break;
	case CMD_DEL:
		if (uci_del(ctx, p, section, option) != UCI_OK) {
			uci_perror(ctx, appname);
			return 1;
		}
		break;
	}

	/* no save necessary for get */
	if (cmd == CMD_GET)
		return 0;

	/* save changes, but don't commit them yet */
	if (uci_save(ctx, p) != UCI_OK) {
		uci_perror(ctx, appname);
		return 1;
	}

	return 0;
}

static int uci_cmd(int argc, char **argv)
{
	int cmd;

	if (!strcasecmp(argv[0], "show"))
		return uci_show(argc, argv);
	if (!strcasecmp(argv[0], "export"))
		return uci_do_export(argc, argv);

	if (!strcasecmp(argv[0], "get"))
		cmd = CMD_GET;
	else if (!strcasecmp(argv[0], "set"))
		cmd = CMD_SET;
	else if (!strcasecmp(argv[0], "del"))
		cmd = CMD_DEL;
	else
		return 255;
	return uci_do_cmd(cmd, argc, argv);
}

int main(int argc, char **argv)
{
	int ret;
	int c;

	ctx = uci_alloc_context();
	if (!ctx) {
		fprintf(stderr, "Out of memory\n");
		return 1;
	}

	while((c = getopt(argc, argv, "sS")) != -1) {
		switch(c) {
			case 's':
				ctx->flags |= UCI_FLAG_STRICT;
				break;
			case 'S':
				ctx->flags &= ~UCI_FLAG_STRICT;
				ctx->flags |= UCI_FLAG_PERROR;
				break;
			default:
				uci_usage(argc, argv);
				break;
		}
	}
	if (optind > 1)
		argv[optind - 1] = argv[0];
	argv += optind - 1;
	argc -= optind - 1;

	if (argc < 2)
		uci_usage(argc, argv);
	ret = uci_cmd(argc - 1, argv + 1);
	if (ret == 255)
		uci_usage(argc, argv);

	uci_free_context(ctx);

	return ret;
}
