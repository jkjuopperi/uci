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
#include "libuci.h"

int main(int argc, char **argv)
{
	struct uci_context *ctx = uci_alloc();

	if (!ctx) {
		fprintf(stderr, "Failed to allocate uci context");
		return 1;
	}
	
	if (uci_parse(ctx, argv[1])) {
		uci_perror(ctx, "uci_parse");
		return 1;
	}
	
	return 0;
}
