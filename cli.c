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
#include "uci.h"

static struct uci_context *ctx;

static void uci_usage(int argc, char **argv)
{
	fprintf(stderr,
		"Usage: %s [<options>] <command> [<arguments>]\n\n"
		"Commands:\n"
		"\tshow [<config>[.<section>[.<option>]]]\n"
		"\texport [<config>]\n"
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

static int uci_show(int argc, char **argv)
{
	char *section = (argc > 2 ? argv[2] : NULL);
	struct uci_package *package;
	struct uci_element *e;
	char **configs;
	char **p;

	configs = uci_list_configs(ctx);
	if (!configs)
		return 0;

	for (p = configs; *p; p++) {
		if ((argc < 2) || !strcmp(argv[1], *p)) {
			if (uci_load(ctx, *p, &package) != UCI_OK) {
				uci_perror(ctx, "uci_load");
				return 255;
			}
			uci_foreach_element( &package->sections, e) {
				if (!section || !strcmp(e->name, section))
					uci_show_section(uci_to_section(e));
			}
			uci_unload(ctx, *p);
		}
	}

	return 0;
}

static int uci_do_export(int argc, char **argv)
{
	char **configs = uci_list_configs(ctx);
	char **p;

	if (!configs)
		return 0;

	for (p = configs; *p; p++) {
		if ((argc < 2) || !strcmp(argv[1], *p)) {
			struct uci_package *package = NULL;
			int ret;

			ret = uci_load(ctx, *p, &package);
			if (ret)
				continue;
			uci_export(ctx, stdout, package);
			uci_unload(ctx, *p);
		}
	}
	return 0;
}

static int uci_cmd(int argc, char **argv)
{
	if (!strcasecmp(argv[0], "show"))
		return uci_show(argc, argv);
	if (!strcasecmp(argv[0], "export"))
		return uci_do_export(argc, argv);
	return 255;
}

int main(int argc, char **argv)
{
	int ret;

	ctx = uci_alloc();
	if (argc < 2)
		uci_usage(argc, argv);
	ret = uci_cmd(argc - 1, argv + 1);
	if (ret == 255)
		uci_usage(argc, argv);
	uci_free(ctx);

	return ret;
}
