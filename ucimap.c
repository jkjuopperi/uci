/*
 * ucimap - library for mapping uci sections into data structures
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
#include "ucimap.h"

struct uci_alloc {
	enum ucimap_type type;
	union {
		void **ptr;
		struct list_head *list;
	} data;
};

struct uci_fixup {
	struct list_head list;
	struct uci_sectmap *sm;
	const char *name;
	struct uci_alloc target;
};

struct uci_sectmap_data {
	struct list_head list;
	struct uci_map *map;
	struct uci_sectmap *sm;
	const char *section_name;

	/* list of allocations done by ucimap */
	struct uci_alloc *allocmap;
	unsigned long allocmap_len;

	/* map for changed fields */
	unsigned char *cmap;
	bool done;
};


int
ucimap_init(struct uci_map *map)
{
	INIT_LIST_HEAD(&map->sdata);
	INIT_LIST_HEAD(&map->fixup);
	return 0;
}

static void
ucimap_free_item(struct uci_alloc *a)
{
	struct list_head *p, *tmp;
	switch(a->type & UCIMAP_TYPE) {
	case UCIMAP_SIMPLE:
		free(a->data.ptr);
		break;
	case UCIMAP_LIST:
		list_for_each_safe(p, tmp, a->data.list) {
			struct uci_listmap *l = list_entry(p, struct uci_listmap, list);
			list_del(p);
			free(l);
		}
		break;
	}
}

static inline void
ucimap_add_alloc(struct uci_alloc *a, void *ptr)
{
	a->type = UCIMAP_SIMPLE;
	a->data.ptr = ptr;
}

static void
ucimap_free_section(struct uci_map *map, struct uci_sectmap_data *sd)
{
	void *section = sd;
	int i;

	section = (char *) section + sizeof(struct uci_sectmap_data);
	if (!list_empty(&sd->list))
		list_del(&sd->list);

	if (sd->sm->free_section)
		sd->sm->free_section(map, section);

	for (i = 0; i < sd->allocmap_len; i++) {
		ucimap_free_item(&sd->allocmap[i]);
	}

	free(sd->allocmap);
	free(sd);
}

void
ucimap_cleanup(struct uci_map *map)
{
	struct list_head *ptr, *tmp;

	list_for_each_safe(ptr, tmp, &map->sdata) {
		struct uci_sectmap_data *sd = list_entry(ptr, struct uci_sectmap_data, list);
		ucimap_free_section(map, sd);
	}
}

static void
ucimap_add_fixup(struct uci_map *map, void *data, struct uci_optmap *om, const char *str)
{
	struct uci_fixup *f;

	f = malloc(sizeof(struct uci_fixup));
	if (!f)
		return;

	INIT_LIST_HEAD(&f->list);
	f->sm = om->data.sm;
	f->name = str;
	f->target.type = om->type;
	f->target.data.ptr = data;
	list_add(&f->list, &map->fixup);
}

static void
ucimap_add_value(union uci_datamap *data, struct uci_optmap *om, struct uci_sectmap_data *sd, const char *str)
{
	union uci_datamap tdata;
	struct list_head *list = NULL;
	char *eptr = NULL;
	char *s;
	int val;

	if ((om->type & UCIMAP_TYPE) == UCIMAP_LIST) {
		if ((om->type & UCIMAP_SUBTYPE) == UCIMAP_SECTION) {
			ucimap_add_fixup(sd->map, data, om, str);
			return;
		}
		memset(&tdata, 0, sizeof(tdata));
		list = &data->list;
		data = &tdata;
	}

	switch(om->type & UCIMAP_SUBTYPE) {
	case UCIMAP_STRING:
		if ((om->data.s.maxlen > 0) &&
			(strlen(str) > om->data.s.maxlen))
			return;

		s = strdup(str);
		data->s = s;
		ucimap_add_alloc(&sd->allocmap[sd->allocmap_len++], s);
		break;
	case UCIMAP_BOOL:
		val = -1;
		if (strcmp(str, "on"))
			val = true;
		else if (strcmp(str, "1"))
			val = true;
		else if (strcmp(str, "enabled"))
			val = true;
		else if (strcmp(str, "off"))
			val = false;
		else if (strcmp(str, "0"))
			val = false;
		else if (strcmp(str, "disabled"))
			val = false;
		if (val == -1)
			return;

		data->b = val;
		break;
	case UCIMAP_INT:
		val = strtol(str, &eptr, om->data.i.base);
		if (!eptr || *eptr == '\0')
			data->i = val;
		else
			return;
		break;
	case UCIMAP_SECTION:
		ucimap_add_fixup(sd->map, data, om, str);
		break;
	}

	if ((om->type & UCIMAP_TYPE) == UCIMAP_LIST) {
		struct uci_listmap *item;

		item = malloc(sizeof(struct uci_listmap));
		if (!item)
			return;

		INIT_LIST_HEAD(&item->list);
		memcpy(&item->data, &tdata, sizeof(tdata));
		list_add(&item->list, list);
	}
}


static int
ucimap_parse_options(struct uci_map *map, struct uci_sectmap *sm, struct uci_sectmap_data *sd, struct uci_section *s)
{
	struct uci_element *e, *l;
	struct uci_option *o;
	unsigned long section;
	union uci_datamap *data;
	int i;

	section = (unsigned long) sd + sizeof(struct uci_sectmap_data);
	uci_foreach_element(&s->options, e) {
		struct uci_optmap *om = NULL;

		for (i = 0; i < sm->n_options; i++) {
			if (strcmp(e->name, sm->options[i].name) == 0) {
				om = &sm->options[i];
				break;
			}
		}
		if (!om)
			continue;

		data = (union uci_datamap *) (section + om->offset);
		o = uci_to_option(e);
		if ((o->type == UCI_TYPE_STRING) && ((om->type & UCIMAP_TYPE) == UCIMAP_SIMPLE)) {
			ucimap_add_value(data, om, sd, o->v.string);
			continue;
		}
		if ((o->type == UCI_TYPE_LIST) && ((om->type & UCIMAP_TYPE) == UCIMAP_LIST)) {
			struct list_head *list;

			list = (struct list_head *) (section + om->offset);
			INIT_LIST_HEAD(list);
			sd->allocmap[sd->allocmap_len].type = UCIMAP_LIST;
			sd->allocmap[sd->allocmap_len++].data.list = list;
			uci_foreach_element(&o->v.list, l) {
				ucimap_add_value(data, om, sd, l->name);
			}
			continue;
		}
	}

	return 0;
}


static int
ucimap_parse_section(struct uci_map *map, struct uci_sectmap *sm, struct uci_section *s)
{
	struct uci_sectmap_data *sd = NULL;
	void *section = NULL;
	char *section_name;
	int err;

	sd = malloc(sm->alloc_len + sizeof(struct uci_sectmap_data));
	if (!sd)
		return UCI_ERR_MEM;

	memset(sd, 0, sm->alloc_len + sizeof(struct uci_sectmap_data));
	INIT_LIST_HEAD(&sd->list);

	sd->map = map;
	sd->sm = sm;
	sd->allocmap = malloc(sm->n_options * sizeof(struct uci_alloc));
	if (!sd->allocmap)
		goto error_mem;

	section_name = strdup(s->e.name);
	if (!section_name)
		goto error_mem;

	sd->section_name = section_name;

	sd->cmap = malloc(BITFIELD_SIZE(sm->n_options));
	if (!sd->cmap)
		goto error_mem;

	memset(sd->cmap, 0, BITFIELD_SIZE(sm->n_options));
	ucimap_add_alloc(&sd->allocmap[sd->allocmap_len++], (void *)section_name);
	ucimap_add_alloc(&sd->allocmap[sd->allocmap_len++], (void *)sd->cmap);

	section = (char *)sd + sizeof(struct uci_sectmap_data);

	err = sm->init_section(map, section, s);
	if (err)
		goto error;

	list_add(&sd->list, &map->sdata);
	err = ucimap_parse_options(map, sm, sd, s);
	if (err)
		goto error;

	return 0;

error_mem:
	if (sd->allocmap)
		free(sd->allocmap);
	free(sd);
	return UCI_ERR_MEM;

error:
	ucimap_free_section(map, sd);
	return err;
}

static int
ucimap_fill_ptr(struct uci_ptr *ptr, struct uci_section *s, const char *option)
{
	struct uci_package *p = s->package;

	memset(ptr, 0, sizeof(struct uci_ptr));

	ptr->package = p->e.name;
	ptr->p = p;

	ptr->section = s->e.name;
	ptr->s = s;

	ptr->option = option;
	return uci_lookup_ptr(p->ctx, ptr, NULL, false);
}

void
ucimap_set_changed(void *section, void *field)
{
	char *sptr = (char *)section - sizeof(struct uci_sectmap_data);
	struct uci_sectmap_data *sd = (struct uci_sectmap_data *) sptr;
	struct uci_sectmap *sm = sd->sm;
	int ofs = (char *)field - (char *)section;
	int i;

	for (i = 0; i < sm->n_options; i++) {
		if (sm->options[i].offset == ofs) {
			SET_BIT(sd->cmap, i);
			break;
		}
	}
}

int
ucimap_store_section(struct uci_map *map, struct uci_package *p, void *section)
{
	char *sptr = (char *)section - sizeof(struct uci_sectmap_data);
	struct uci_sectmap_data *sd = (struct uci_sectmap_data *) sptr;
	struct uci_sectmap *sm = sd->sm;
	struct uci_section *s = NULL;
	struct uci_element *e;
	struct uci_ptr ptr;
	int i, ret;

	uci_foreach_element(&p->sections, e) {
		if (!strcmp(e->name, sd->section_name)) {
			s = uci_to_section(e);
			break;
		}
	}
	if (!s)
		return UCI_ERR_NOTFOUND;

	for (i = 0; i < sm->n_options; i++) {
		struct uci_optmap *om = &sm->options[i];
		static char buf[32];
		const char *str = NULL;
		void *p = (char *)section + om->offset;

		if (!TEST_BIT(sd->cmap, i))
			continue;

		ucimap_fill_ptr(&ptr, s, om->name);
		switch(om->type & UCIMAP_SUBTYPE) {
		case UCIMAP_STRING:
			str = *((char **) p);
			break;
		case UCIMAP_INT:
			sprintf(buf, "%d", *((int *) p));
			str = buf;
			break;
		case UCIMAP_BOOL:
			sprintf(buf, "%d", !!*((bool *)p));
			str = buf;
			break;
		}
		ptr.value = str;

		ret = uci_set(s->package->ctx, &ptr);
		if (ret)
			return ret;

		CLR_BIT(sd->cmap, i);
	}

	return 0;
}

void *
ucimap_find_section(struct uci_map *map, struct uci_fixup *f)
{
	struct uci_sectmap_data *sd;
	struct list_head *p;
	void *ret;

	list_for_each(p, &map->sdata) {
		sd = list_entry(p, struct uci_sectmap_data, list);
		if (sd->sm != f->sm)
			continue;
		if (strcmp(f->name, sd->section_name) != 0)
			continue;
		ret = (char *)sd + sizeof(struct uci_sectmap_data);
		return ret;
	}
	return NULL;
}

void
ucimap_parse(struct uci_map *map, struct uci_package *pkg)
{
	struct uci_element *e;
	struct list_head *p, *tmp;
	int i;

	INIT_LIST_HEAD(&map->fixup);
	uci_foreach_element(&pkg->sections, e) {
		struct uci_section *s = uci_to_section(e);

		for (i = 0; i < map->n_sections; i++) {
			if (strcmp(s->type, map->sections[i]->type) != 0)
				continue;
			ucimap_parse_section(map, map->sections[i], s);
		}
	}
	list_for_each_safe(p, tmp, &map->fixup) {
		struct uci_fixup *f = list_entry(p, struct uci_fixup, list);
		void *ptr = ucimap_find_section(map, f);
		struct uci_listmap *li;

		if (!ptr)
			continue;

		switch(f->target.type & UCIMAP_TYPE) {
		case UCIMAP_SIMPLE:
			*f->target.data.ptr = ptr;
			break;
		case UCIMAP_LIST:
			li = malloc(sizeof(struct uci_listmap));
			memset(li, 0, sizeof(struct uci_listmap));
			INIT_LIST_HEAD(&li->list);
			li->data.section = ptr;
			list_add(&li->list, f->target.data.list);
			break;
		}
	}
	list_for_each_safe(p, tmp, &map->sdata) {
		struct uci_sectmap_data *sd = list_entry(p, struct uci_sectmap_data, list);
		void *section;

		if (sd->done)
			continue;

		section = (char *) sd + sizeof(struct uci_sectmap_data);
		if (sd->sm->add_section(map, section) != 0)
			ucimap_free_section(map, sd);
	}
}
