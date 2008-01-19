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

/* initialize a list head/item */
static inline void uci_list_init(struct uci_list *ptr)
{
	ptr->prev = ptr;
	ptr->next = ptr;
}

/* inserts a new list entry between two consecutive entries */
static inline void __uci_list_add(struct uci_list *prev, struct uci_list *next, struct uci_list *ptr)
{
	prev->next = ptr;
	next->prev = ptr;
	ptr->prev = prev;
	ptr->next = next;
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

static struct uci_config *uci_alloc_file(struct uci_context *ctx, const char *name)
{
	struct uci_config *cfg;

	cfg = (struct uci_config *) uci_malloc(ctx, sizeof(struct uci_config));
	uci_list_init(&cfg->list);
	uci_list_init(&cfg->sections);
	cfg->name = uci_strdup(ctx, name);
	cfg->ctx = ctx;

	return cfg;
}

static void uci_drop_file(struct uci_config *cfg)
{
	/* TODO: free children */
	uci_list_del(&cfg->list);
	if (cfg->name)
		free(cfg->name);
	free(cfg);
}
