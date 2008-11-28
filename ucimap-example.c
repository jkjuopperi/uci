/*
 * ucimap-example - sample code for the ucimap library
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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ucimap.h>

struct list_head ifs;

struct uci_network {
	struct list_head list;
	struct list_head alias;

	const char *name;
	const char *proto;
	const char *ifname;
	const char *ipaddr;
	int test;
	bool enabled;
	struct list_head aliases;
};

struct uci_alias {
	struct list_head list;

	const char *name;
	struct uci_network *interface;
};

static int
network_init_interface(struct uci_map *map, void *section, struct uci_section *s)
{
	struct uci_network *net = section;

	INIT_LIST_HEAD(&net->list);
	INIT_LIST_HEAD(&net->alias);
	net->name = s->e.name;
	net->test = -1;
	return 0;
}

static int
network_init_alias(struct uci_map *map, void *section, struct uci_section *s)
{
	struct uci_alias *alias = section;

	INIT_LIST_HEAD(&alias->list);
	alias->name = s->e.name;
	return 0;
}

static int
network_add_interface(struct uci_map *map, void *section)
{
	struct uci_network *net = section;

	list_add(&net->list, &ifs);

	return 0;
}

static int
network_add_alias(struct uci_map *map, void *section)
{
	struct uci_alias *a = section;

	if (a->interface)
		list_add(&a->list, &a->interface->alias);

	return 0;
}

struct uci_sectmap network_interface;
struct uci_sectmap network_alias;

struct uci_optmap network_interface_options[] = {
	OPTMAP_OPTION(UCIMAP_STRING, struct uci_network, proto, .data.s.maxlen = 32),
	OPTMAP_OPTION(UCIMAP_STRING, struct uci_network, ifname),
	OPTMAP_OPTION(UCIMAP_STRING, struct uci_network, ipaddr),
	OPTMAP_OPTION(UCIMAP_BOOL, struct uci_network, enabled),
	OPTMAP_OPTION(UCIMAP_INT, struct uci_network, test),
	OPTMAP_OPTION(UCIMAP_LIST | UCIMAP_SECTION, struct uci_network, aliases, .data.sm = &network_alias),
};

struct uci_sectmap network_interface = {
	.type = "interface",
	.options = network_interface_options,
	.alloc_len = sizeof(struct uci_network),
	.init_section = network_init_interface,
	.add_section = network_add_interface,
	.n_options = ARRAY_SIZE(network_interface_options),
};

struct uci_optmap network_alias_options[] = {
	OPTMAP_OPTION(UCIMAP_SECTION, struct uci_alias, interface, .data.sm = &network_interface),
};

struct uci_sectmap network_alias = {
	.type = "alias",
	.options = network_alias_options,
	.alloc_len = sizeof(struct uci_network),
	.init_section = network_init_alias,
	.add_section = network_add_alias,
	.n_options = ARRAY_SIZE(network_alias_options),
};

struct uci_sectmap *network_smap[] = {
	&network_interface,
	&network_alias,
};

struct uci_map network_map = {
	.sections = network_smap,
	.n_sections = ARRAY_SIZE(network_smap),
};


int main(int argc, char **argv)
{
	struct uci_context *ctx;
	struct uci_package *pkg;
	struct list_head *p, *p2;
	struct uci_network *net;
	struct uci_alias *alias;

	INIT_LIST_HEAD(&ifs);
	ctx = uci_alloc_context();
	ucimap_init(&network_map);

	uci_load(ctx, "network", &pkg);

	ucimap_parse(&network_map, pkg);

	list_for_each(p, &ifs) {
		net = list_entry(p, struct uci_network, list);
		printf("New network section '%s'\n"
			"	type: %s\n"
			"	ifname: %s\n"
			"	ipaddr: %s\n"
			"	test: %d\n"
			"	enabled: %s\n",
			net->name,
			net->proto,
			net->ifname,
			net->ipaddr,
			net->test,
			(net->enabled ? "on" : "off"));

		list_for_each(p2, &net->aliases) {
			struct uci_listmap *li = list_entry(p2, struct uci_listmap, list);
			alias = li->data.section;
			printf("New alias: %s\n", alias->name);
		}
#if 0
		net->ipaddr = "2.3.4.5";
		ucimap_set_changed(net, &net->ipaddr);
		ucimap_store_section(&network_map, pkg, net);
		uci_save(ctx, pkg);
#endif
	}


done:
	ucimap_cleanup(&network_map);
	uci_free_context(ctx);

	return 0;
}
