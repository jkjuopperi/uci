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
static inline void uci_list_insert(struct uci_list *list, struct uci_list *ptr)
{
	list->next->prev = ptr;
	ptr->prev = list;
	ptr->next = list->next;
	list->next = ptr;
}

/* inserts a new list entry at the tail of the list */
static inline void uci_list_add(struct uci_list *head, struct uci_list *ptr)
{
	/* NB: head->prev points at the tail */
	uci_list_insert(head->prev, ptr);
}

static inline void uci_list_del(struct uci_list *ptr)
{
	struct uci_list *next, *prev;

	next = ptr->next;
	prev = ptr->prev;

	prev->next = next;
	next->prev = prev;

	uci_list_init(ptr);
}

static struct uci_element *
uci_alloc_generic(struct uci_context *ctx, int type, const char *name, int size)
{
	struct uci_element *e;
	void *ptr;

	ptr = uci_malloc(ctx, size + strlen(name) + 1);
	e = (struct uci_element *) ptr;
	e->type = type;
	e->name = (char *) ptr + size;
	strcpy(e->name, name);
	uci_list_init(&e->list);

	return e;
}

static void
uci_free_element(struct uci_element *e)
{
	if (!e)
		return;

	if (!uci_list_empty(&e->list))
		uci_list_del(&e->list);
	free(e);
}

static struct uci_option *
uci_alloc_option(struct uci_section *s, const char *name, const char *value)
{
	struct uci_package *p = s->package;
	struct uci_context *ctx = p->ctx;
	struct uci_option *o;

	o = uci_alloc_element(ctx, option, name, strlen(value) + 1);
	o->value = uci_dataptr(o);
	o->section = s;
	strcpy(o->value, value);
	uci_list_add(&s->options, &o->e.list);

	return o;
}

static inline void
uci_free_option(struct uci_option *o)
{
	uci_free_element(&o->e);
}

static struct uci_section *
uci_alloc_section(struct uci_package *p, const char *type, const char *name)
{
	struct uci_context *ctx = p->ctx;
	struct uci_section *s;
	char buf[16];

	if (!name || !name[0]) {
		snprintf(buf, 16, "cfg%d", p->n_section);
		name = buf;
	}

	s = uci_alloc_element(ctx, section, name, strlen(type) + 1);
	s->type = uci_dataptr(s);
	s->package = p;
	strcpy(s->type, type);
	uci_list_init(&s->options);
	uci_list_add(&p->sections, &s->e.list);

	return s;
}

static void
uci_free_section(struct uci_section *s)
{
	struct uci_element *o, *tmp;

	uci_foreach_element_safe(&s->options, tmp, o) {
		uci_free_option(uci_to_option(o));
	}
	uci_free_element(&s->e);
}

static struct uci_package *
uci_alloc_package(struct uci_context *ctx, const char *name)
{
	struct uci_package *p;

	p = uci_alloc_element(ctx, package, name, 0);
	p->ctx = ctx;
	uci_list_init(&p->sections);
	return p;
}

static void
uci_free_package(struct uci_package *p)
{
	struct uci_element *e, *tmp;

	if(!p)
		return;

	uci_foreach_element_safe(&p->sections, tmp, e) {
		uci_free_section(uci_to_section(e));
	}
	uci_free_element(&p->e);
}

static struct uci_element *uci_lookup_list(struct uci_context *ctx, struct uci_list *list, char *name)
{
	struct uci_element *e;

	uci_foreach_element(list, e) {
		if (!strcmp(e->name, name))
			return e;
	}
	UCI_THROW(ctx, UCI_ERR_NOTFOUND);
}

int uci_lookup(struct uci_context *ctx, struct uci_element **res, char *package, char *section, char *option)
{
	struct uci_element *e;
	struct uci_package *p;
	struct uci_section *s;
	struct uci_option *o;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, res != NULL);
	UCI_ASSERT(ctx, package != NULL);

	e = uci_lookup_list(ctx, &ctx->root, package);
	if (!section)
		goto found;

	p = uci_to_package(e);
	e = uci_lookup_list(ctx, &p->sections, section);
	if (!option)
		goto found;

	s = uci_to_section(e);
	e = uci_lookup_list(ctx, &s->options, option);

found:
	*res = e;
	return 0;
}

int uci_unload(struct uci_context *ctx, const char *name)
{
	struct uci_element *e;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, name != NULL);

	uci_foreach_element(&ctx->root, e) {
		if (!strcmp(e->name, name))
			goto found;
	}
	UCI_THROW(ctx, UCI_ERR_NOTFOUND);

found:
	uci_free_package(uci_to_package(e));

	return 0;
}


