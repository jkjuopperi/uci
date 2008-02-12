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

#include <glob.h>

/* initialize a list head/item */
static inline void uci_list_init(struct uci_list *ptr)
{
	ptr->prev = ptr;
	ptr->next = ptr;
}

/* inserts a new list entry after a given entry */
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

/* 
 * uci_alloc_generic allocates a new uci_element with payload
 * payload is appended to the struct to save memory and reduce fragmentation
 */
static struct uci_element *
uci_alloc_generic(struct uci_context *ctx, int type, const char *name, int size)
{
	struct uci_element *e;
	int datalen = size;
	void *ptr;

	ptr = uci_malloc(ctx, datalen);
	e = (struct uci_element *) ptr;
	e->type = type;
	if (name) {
		UCI_TRAP_SAVE(ctx, error);
		e->name = uci_strdup(ctx, name);
		UCI_TRAP_RESTORE(ctx);
	}
	uci_list_init(&e->list);
	goto done;

error:
	free(ptr);
	UCI_THROW(ctx, ctx->errno);

done:
	return e;
}

static void
uci_free_element(struct uci_element *e)
{
	if (e->name)
		free(e->name);
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
	if ((o->value != uci_dataptr(o)) &&
		(o->value != NULL))
		free(o->value);
	uci_free_element(&o->e);
}

/* fix up an unnamed section, e.g. after adding options to it */
static void uci_fixup_section(struct uci_context *ctx, struct uci_section *s)
{
	unsigned int hash = ~0;
	struct uci_element *e;
	char buf[16];

	if (!s || s->e.name)
		return;

	/*
	 * Generate a name for unnamed sections. This is used as reference
	 * when locating or updating the section from apps/scripts.
	 * To make multiple concurrent versions somewhat safe for updating,
	 * the name is generated from a hash of its type and name/value
	 * pairs of its option, and it is prefixed by a counter value.
	 * If the order of the unnamed sections changes for some reason,
	 * updates to them will be rejected.
	 */
	hash = djbhash(hash, s->type);
	uci_foreach_element(&s->options, e) {
		hash = djbhash(hash, e->name);
		hash = djbhash(hash, uci_to_option(e)->value);
	}
	sprintf(buf, "cfg%02x%04x", ++s->package->n_section, hash % (1 << 16));
	s->e.name = uci_strdup(ctx, buf);
}

static struct uci_section *
uci_alloc_section(struct uci_package *p, const char *type, const char *name)
{
	struct uci_context *ctx = p->ctx;
	struct uci_section *s;

	if (name && !name[0])
		name = NULL;

	s = uci_alloc_element(ctx, section, name, strlen(type) + 1);
	uci_list_init(&s->options);
	s->type = uci_dataptr(s);
	s->package = p;
	strcpy(s->type, type);
	if (name == NULL)
		s->anonymous = true;
	p->n_section++;

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
	if ((s->type != uci_dataptr(s)) &&
		(s->type != NULL))
		free(s->type);
	uci_free_element(&s->e);
}

static struct uci_package *
uci_alloc_package(struct uci_context *ctx, const char *name)
{
	struct uci_package *p;

	p = uci_alloc_element(ctx, package, name, 0);
	p->ctx = ctx;
	uci_list_init(&p->sections);
	uci_list_init(&p->history);
	uci_list_init(&p->saved_history);
	return p;
}

static void
uci_free_package(struct uci_package **package)
{
	struct uci_element *e, *tmp;
	struct uci_package *p = *package;

	if(!p)
		return;

	if (p->path)
		free(p->path);
	uci_foreach_element_safe(&p->sections, tmp, e) {
		uci_free_section(uci_to_section(e));
	}
	uci_foreach_element_safe(&p->history, tmp, e) {
		uci_free_history(uci_to_history(e));
	}
	uci_foreach_element_safe(&p->saved_history, tmp, e) {
		uci_free_history(uci_to_history(e));
	}
	uci_free_element(&p->e);
	*package = NULL;
}

static struct uci_element *uci_lookup_list(struct uci_context *ctx, struct uci_list *list, const char *name)
{
	struct uci_element *e;

	uci_foreach_element(list, e) {
		if (!strcmp(e->name, name))
			return e;
	}
	return NULL;
}

int uci_lookup(struct uci_context *ctx, struct uci_element **res, struct uci_package *p, char *section, char *option)
{
	struct uci_element *e;
	struct uci_section *s;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, res != NULL);
	UCI_ASSERT(ctx, p != NULL);
	UCI_ASSERT(ctx, uci_validate_name(section));
	if (option)
		UCI_ASSERT(ctx, uci_validate_name(option));

	e = uci_lookup_list(ctx, &p->sections, section);
	if (!e)
		goto notfound;

	if (option) {
		s = uci_to_section(e);
		e = uci_lookup_list(ctx, &s->options, option);
		if (!e)
			goto notfound;
	}

	*res = e;
	return 0;

notfound:
	UCI_THROW(ctx, UCI_ERR_NOTFOUND);
	return 0;
}

int uci_del_element(struct uci_context *ctx, struct uci_element *e)
{
	/* NB: UCI_INTERNAL use means without history tracking */
	bool internal = ctx->internal;
	struct uci_package *p = NULL;
	struct uci_section *s = NULL;
	struct uci_option *o = NULL;
	struct uci_element *i, *tmp;
	char *option = NULL;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, e != NULL);

	switch(e->type) {
	case UCI_TYPE_SECTION:
		s = uci_to_section(e);
		uci_foreach_element_safe(&s->options, tmp, i) {
			uci_del_element(ctx, i);
		}
		break;
	case UCI_TYPE_OPTION:
		o = uci_to_option(e);
		s = o->section;
		p = s->package;
		option = e->name;
		break;
	default:
		UCI_THROW(ctx, UCI_ERR_INVAL);
		break;
	}

	p = s->package;
	if (!internal && p->has_history)
		uci_add_history(ctx, &p->history, UCI_CMD_REMOVE, s->e.name, option, NULL);

	switch(e->type) {
	case UCI_TYPE_SECTION:
		uci_free_section(s);
		break;
	case UCI_TYPE_OPTION:
		uci_free_option(o);
		break;
	default:
		break;
	}
	return 0;
}

int uci_set_element_value(struct uci_context *ctx, struct uci_element **element, char *value)
{
	/* NB: UCI_INTERNAL use means without history tracking */
	bool internal = ctx->internal;
	struct uci_list *list;
	struct uci_element *e;
	struct uci_package *p;
	struct uci_section *s;
	char *section;
	char *option;
	char *str;
	int size;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, (element != NULL) && (*element != NULL));

	/* what the 'value' of an element means depends on the type
	 * for a section, the 'value' means its type
	 * for an option, the 'value' means its value string
	 * when changing the value, shrink the element to its actual size
	 * (it may have been allocated with a bigger size, to include
	 *  its buffer)
	 * then duplicate the string passed on the command line and
	 * insert it into the structure.
	 */
	e = *element;
	list = e->list.prev;
	switch(e->type) {
	case UCI_TYPE_SECTION:
		UCI_ASSERT(ctx, uci_validate_str(value, false));
		size = sizeof(struct uci_section);
		s = uci_to_section(e);
		section = e->name;
		option = NULL;
		break;
	case UCI_TYPE_OPTION:
		UCI_ASSERT(ctx, value != NULL);
		size = sizeof(struct uci_option);
		s = uci_to_option(e)->section;
		section = s->e.name;
		option = e->name;
		break;
	default:
		UCI_THROW(ctx, UCI_ERR_INVAL);
		return 0;
	}
	p = s->package;
	if (!internal && p->has_history)
		uci_add_history(ctx, &p->history, UCI_CMD_CHANGE, section, option, value);

	uci_list_del(&e->list);
	e = uci_realloc(ctx, e, size);
	str = uci_strdup(ctx, value);
	uci_list_insert(list, &e->list);
	*element = e;

	switch(e->type) {
	case UCI_TYPE_SECTION:
		uci_to_section(e)->type = str;
		break;
	case UCI_TYPE_OPTION:
		uci_to_option(e)->value = str;
		break;
	default:
		break;
	}

	return 0;
}

int uci_rename(struct uci_context *ctx, struct uci_package *p, char *section, char *option, char *name)
{
	/* NB: UCI_INTERNAL use means without history tracking */
	bool internal = ctx->internal;
	struct uci_element *e;

	UCI_HANDLE_ERR(ctx);

	/* NB: p, section, option validated by uci_lookup */
	UCI_INTERNAL(uci_lookup, ctx, &e, p, section, option);

	if (!internal && p->has_history)
		uci_add_history(ctx, &p->history, UCI_CMD_RENAME, section, option, name);

	name = uci_strdup(ctx, name);
	if (e->name)
		free(e->name);
	e->name = name;

	return 0;
}

int uci_add_section(struct uci_context *ctx, struct uci_package *p, char *type, struct uci_section **res)
{
	struct uci_section *s;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, p != NULL);
	s = uci_alloc_section(p, type, NULL);
	uci_fixup_section(ctx, s);
	*res = s;
	uci_add_history(ctx, &p->history, UCI_CMD_ADD, s->e.name, NULL, type);

	return 0;
}

int uci_delete(struct uci_context *ctx, struct uci_package *p, char *section, char *option)
{
	/* NB: pass on internal flag to uci_del_element */
	bool internal = ctx->internal;
	struct uci_element *e;

	UCI_HANDLE_ERR(ctx);

	/* NB: p, section, option validated by uci_lookup */
	UCI_INTERNAL(uci_lookup, ctx, &e, p, section, option);

	ctx->internal = internal;
	return uci_del_element(ctx, e);
}

int uci_set(struct uci_context *ctx, struct uci_package *p, char *section, char *option, char *value, struct uci_element **result)
{
	/* NB: UCI_INTERNAL use means without history tracking */
	bool internal = ctx->internal;
	struct uci_element *e = NULL;
	struct uci_section *s = NULL;
	struct uci_option *o = NULL;

	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, p != NULL);
	UCI_ASSERT(ctx, uci_validate_name(section));
	if (option) {
		UCI_ASSERT(ctx, uci_validate_name(option));
		UCI_ASSERT(ctx, value != NULL);
	} else {
		UCI_ASSERT(ctx, uci_validate_str(value, false));
	}

	/*
	 * look up the package, section and option (if set)
	 * if the section/option is to be modified and it is not found
	 * create a new element in the appropriate list
	 */
	e = uci_lookup_list(ctx, &p->sections, section);
	if (!e)
		goto notfound;

	s = uci_to_section(e);
	if (ctx->pctx && ctx->pctx->merge)
		ctx->pctx->section = s;

	if (option) {
		e = uci_lookup_list(ctx, &s->options, option);
		if (!e)
			goto notfound;
		o = uci_to_option(e);
	}

	/* 
	 * no unknown element was supplied, assume that we can just update 
	 * an existing entry
	 */
	if (o)
		e = &o->e;
	else
		e = &s->e;
	if (result)
		*result = e;
	else
		result = &e;

	ctx->internal = internal;
	return uci_set_element_value(ctx, result, value);

notfound:
	/* 
	 * the entry that we need to update was not found,
	 * check if the search failed prematurely.
	 * this can happen if the package was not found, or if
	 * an option was supplied, but the section wasn't found
	 */
	if (!p || (!s && option))
		UCI_THROW(ctx, UCI_ERR_NOTFOUND);

	/* now add the missing entry */
	if (!internal && p->has_history)
		uci_add_history(ctx, &p->history, UCI_CMD_CHANGE, section, option, value);
	if (s) {
		o = uci_alloc_option(s, option, value);
		if (result)
			*result = &o->e;
	} else {
		s = uci_alloc_section(p, value, section);
		if (result)
			*result = &s->e;
		if (ctx->pctx && ctx->pctx->merge)
			ctx->pctx->section = s;
	}

	return 0;
}

int uci_unload(struct uci_context *ctx, struct uci_package *p)
{
	UCI_HANDLE_ERR(ctx);
	UCI_ASSERT(ctx, p != NULL);

	uci_free_package(&p);
	return 0;
}

