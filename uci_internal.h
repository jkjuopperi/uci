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

#ifndef __UCI_INTERNAL_H
#define __UCI_INTERNAL_H

struct uci_parse_context
{
	/* error context */
	const char *reason;
	int line;
	int byte;

	/* private: */
	struct uci_package *package;
	struct uci_section *section;
	bool merge;
	FILE *file;
	const char *name;
	char *buf;
	int bufsz;
};

static void uci_add_history(struct uci_context *ctx, struct uci_list *list, int cmd, char *section, char *option, char *value);
static void uci_free_history(struct uci_history *h);

#endif
