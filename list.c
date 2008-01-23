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

#include <glob.h>

/* initialize a list head/item */
static inline void uci_list_init(struct uci_list *ptr)
{
	ptr->prev = ptr;
	ptr->next = ptr;
}

/* inserts a new list entry between two consecutive entries */
static inline void __uci_list_add(struct uci_list *prev, struct uci_list *next, struct uci_list *ptr)
{
	next->prev = ptr;
	ptr->prev = prev;
	ptr->next = next;
	prev->next = ptr;
}

/* inserts a new list entry at the tail of the list */
static inline void uci_list_add(struct uci_list *head, struct uci_list *ptr)
{
	/* NB: head->prev points at the tail */
	__uci_list_add(head->prev, head, ptr);
}

static inline void uci_list_del(struct uci_list *ptr)
{
	struct uci_list *next, *prev;

	next = ptr->next;
	prev = ptr->prev;

	prev->next = next;
	next->prev = prev;
}

static void uci_drop_option(struct uci_option *option)
{
	if (!option)
		return;
	if (option->name)
		free(option->name);
	if (option->value)
		free(option->value);
	free(option);
}

static struct uci_option *uci_add_option(struct uci_section *section, const char *name, const char *value)
{
	struct uci_package *package = section->package;
	struct uci_context *ctx = package->ctx;
	struct uci_option *option = NULL;

	UCI_TRAP_SAVE(ctx, error);
	option = (struct uci_option *) uci_malloc(ctx, sizeof(struct uci_option));
	option->name = uci_strdup(ctx, name);
	option->value = uci_strdup(ctx, value);
	uci_list_add(&section->options, &option->list);
	UCI_TRAP_RESTORE(ctx);
	return option;

error:
	uci_drop_option(option);
	UCI_THROW(ctx, ctx->errno);
	return NULL;
}

static void uci_drop_section(struct uci_section *section)
{
	struct uci_option *opt;

	if (!section)
		return;

	uci_foreach_entry(option, &section->options, opt) {
		uci_list_del(&opt->list);
		uci_drop_option(opt);
	}

	if (section->name)
		free(section->name);
	if (section->type)
		free(section->type);
	free(section);
}

static struct uci_section *uci_add_section(struct uci_package *package, const char *type, const char *name)
{
	struct uci_section *section = NULL;
	struct uci_context *ctx = package->ctx;

	UCI_TRAP_SAVE(ctx, error);
	package->n_section++;
	section = (struct uci_section *) uci_malloc(ctx, sizeof(struct uci_section));
	section->package = package;
	uci_list_init(&section->list);
	uci_list_init(&section->options);
	section->type = uci_strdup(ctx, type);
	if (name && name[0])
		section->name = uci_strdup(ctx, name);
	else
		asprintf(&section->name, "cfg%d", package->n_section);
	uci_list_add(&package->sections, &section->list);
	UCI_TRAP_RESTORE(ctx);

	return section;

error:
	uci_drop_section(section);
	UCI_THROW(ctx, ctx->errno);
	return NULL;
}

static void uci_drop_config(struct uci_package *package)
{
	struct uci_section *s;

	if(!package)
		return;

	uci_foreach_entry(section, &package->sections, s) {
		uci_list_del(&s->list);
		uci_drop_section(s);
	}

	if (package->name)
		free(package->name);
	free(package);
}


static struct uci_package *uci_alloc_config(struct uci_context *ctx, const char *name)
{
	struct uci_package *package = NULL;

	UCI_TRAP_SAVE(ctx, error);
	package = (struct uci_package *) uci_malloc(ctx, sizeof(struct uci_package));
	uci_list_init(&package->list);
	uci_list_init(&package->sections);
	package->name = uci_strdup(ctx, name);
	package->ctx = ctx;
	UCI_TRAP_RESTORE(ctx);
	return package;

error:
	uci_drop_config(package);
	UCI_THROW(ctx, ctx->errno);
	return NULL;
}

int uci_unload(struct uci_context *ctx, const char *name)
{
	struct uci_package *package;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, name != NULL);

	uci_foreach_entry(package, &ctx->root, package) {
		if (!strcmp(package->name, name))
			goto found;
	}
	UCI_THROW(ctx, UCI_ERR_NOTFOUND);

found:
	uci_list_del(&package->list);
	uci_drop_config(package);

	return 0;
}

static inline char *get_filename(char *path)
{
	char *p;

	p = strrchr(path, '/');
	p++;
	if (!*p)
		return NULL;
	return p;
}

char **uci_list_configs()
{
	char **configs;
	glob_t globbuf;
	int size, i;
	char *buf;

	if (glob(UCI_CONFDIR "/*", GLOB_MARK, NULL, &globbuf) != 0)
		return NULL;

	size = sizeof(char *) * (globbuf.gl_pathc + 1);
	for(i = 0; i < globbuf.gl_pathc; i++) {
		char *p;

		p = get_filename(globbuf.gl_pathv[i]);
		if (!p)
			continue;

		size += strlen(p) + 1;
	}

	configs = malloc(size);
	if (!configs)
		return NULL;

	memset(configs, 0, size);
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
	return configs;
}

