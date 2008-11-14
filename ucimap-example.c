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

struct uci_network {
	struct list_head list;

	const char *name;
	const char *proto;
	const char *ifname;
	const char *ipaddr;
	int test;
	bool enabled;
};

static int
network_init_section(struct uci_map *map, void *section, struct uci_section *s)
{
	struct uci_network *net = section;

	INIT_LIST_HEAD(&net->list);
	net->name = s->e.name;
	net->test = -1;
	return 0;
}

static int
network_add_section(struct uci_map *map, void *section)
{
	struct uci_network *net = section;
	struct uci_network **nptr = map->priv;

	*nptr = net;
	return 0;
}

struct uci_optmap network_smap_options[] = {
	OPTMAP_OPTION(UCIMAP_STRING, struct uci_network, proto, .data.s.maxlen = 32),
	OPTMAP_OPTION(UCIMAP_STRING, struct uci_network, ifname),
	OPTMAP_OPTION(UCIMAP_STRING, struct uci_network, ipaddr),
	OPTMAP_OPTION(UCIMAP_BOOL, struct uci_network, enabled),
	OPTMAP_OPTION(UCIMAP_INT, struct uci_network, test),
};

struct uci_sectmap network_smap[] = {
	{
		.type = "interface",
		.options = network_smap_options,
		.alloc_len = sizeof(struct uci_network),
		.init_section = network_init_section,
		.add_section = network_add_section,
		.n_options = ARRAY_SIZE(network_smap_options),
	}
};

struct uci_map network_map = {
	.sections = network_smap,
	.n_sections = ARRAY_SIZE(network_smap),
};


int main(int argc, char **argv)
{
	struct uci_context *ctx;
	struct uci_package *pkg;
	struct uci_network *net = NULL;

	ctx = uci_alloc_context();
	ucimap_init(&network_map);

	uci_load(ctx, "network", &pkg);

	network_map.priv = &net;
	ucimap_parse(&network_map, pkg);

	if (!net)
		goto done;

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

	net->ipaddr = "2.3.4.5";
	ucimap_set_changed(net, &net->ipaddr);
	ucimap_store_section(&network_map, pkg, net);
	uci_save(ctx, pkg);

done:
	ucimap_cleanup(&network_map);
	uci_free_context(ctx);

	return 0;
}
