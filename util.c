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
 * This file contains wrappers to standard functions, which
 * throw exceptions upon failure.
 */
#include <stdbool.h>
#include <ctype.h>

static void *uci_malloc(struct uci_context *ctx, size_t size)
{
	void *ptr;

	ptr = malloc(size);
	if (!ptr)
		UCI_THROW(ctx, UCI_ERR_MEM);
	memset(ptr, 0, size);

	return ptr;
}

static void *uci_realloc(struct uci_context *ctx, void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (!ptr)
		UCI_THROW(ctx, UCI_ERR_MEM);

	return ptr;
}

static char *uci_strdup(struct uci_context *ctx, const char *str)
{
	char *ptr;

	ptr = strdup(str);
	if (!ptr)
		UCI_THROW(ctx, UCI_ERR_MEM);

	return ptr;
}

static bool validate_name(char *str)
{
	if (!*str)
		return false;

	while (*str) {
		if (!isalnum(*str) && (*str != '_'))
			return false;
		str++;
	}
	return true;
}

int uci_parse_tuple(struct uci_context *ctx, char *str, char **package, char **section, char **option, char **value)
{
	char *last = NULL;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, str && package && section && option);

	*package = strtok(str, ".");
	if (!*package || !validate_name(*package))
		goto error;

	last = *package;
	*section = strtok(NULL, ".");
	if (!*section)
		goto lastval;

	last = *section;
	*option = strtok(NULL, ".");
	if (!*option)
		goto lastval;

	last = *option;

lastval:
	last = strchr(last, '=');
	if (last) {
		if (!value)
			goto error;

		*last = 0;
		last++;
		if (!*last)
			goto error;
	}

	if (*section && !validate_name(*section))
		goto error;
	if (*option && !validate_name(*option))
		goto error;

	goto done;

error:
	UCI_THROW(ctx, UCI_ERR_PARSE);

done:
	return 0;
}

