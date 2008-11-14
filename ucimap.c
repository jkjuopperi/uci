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


int
ucimap_init(struct uci_map *map)
{
	INIT_LIST_HEAD(&map->sdata);
	return 0;
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
		free(sd->allocmap[i]);
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


static int
ucimap_parse_options(struct uci_map *map, struct uci_sectmap *sm, struct uci_sectmap_data *sd, struct uci_section *s)
{
	struct uci_element *e;
	struct uci_option *o;
	void *section = sd;
	int i;

	section = (unsigned char *) section + sizeof(struct uci_sectmap_data);
	uci_foreach_element(&s->options, e) {
		struct uci_optmap *om = NULL;

		for (i = 0; i < sm->n_options; i++) {
			if (strcmp(e->name, sm->options[i].name) == 0) {
				om = &sm->options[i];
				break;
			}
		}
		if (!om)
			goto next_element;

		o = uci_to_option(e);
		if(o->type != UCI_TYPE_STRING)
			goto next_element;

		switch(om->type) {
		case UCIMAP_STRING: {
			char **ptr;
			if ((om->data.s.maxlen > 0) &&
				(strlen(o->v.string) > om->data.s.maxlen))
				goto next_element;

			ptr = (char **) ((char *) section + om->offset);
			*ptr = strdup(o->v.string);
			sd->allocmap[sd->allocmap_len++] = *ptr;
			} break;
		case UCIMAP_BOOL: {
			bool *ptr = (bool *)((char *)section + om->offset);
			if (strcmp(o->v.string, "on"))
				*ptr = true;
			else if (strcmp(o->v.string, "1"))
				*ptr = true;
			else if (strcmp(o->v.string, "enabled"))
				*ptr = true;
			else
				*ptr = false;
			} break;
		case UCIMAP_INT: {
			int *ptr = (int *)((char *)section + om->offset);
			char *eptr = NULL;
			int val;

			val = strtol(o->v.string, &eptr, om->data.i.base);
			if (!eptr || *eptr == '\0')
				*ptr = val;
			} break;
		}
next_element:
		continue;
	}

	return 0;
}


static int
ucimap_parse_section(struct uci_map *map, struct uci_sectmap *sm, struct uci_section *s)
{
	struct uci_sectmap_data *sd = NULL;
	void *section = NULL;
	int err;

	sd = malloc(sm->alloc_len + sizeof(struct uci_sectmap_data));
	if (!sd)
		return UCI_ERR_MEM;

	memset(sd, 0, sm->alloc_len + sizeof(struct uci_sectmap_data));
	INIT_LIST_HEAD(&sd->list);

	sd->sm = sm;
	sd->allocmap = malloc(sm->n_options * sizeof(void *));
	if (!sd->allocmap)
		goto error_mem;

	sd->section_name = strdup(s->e.name);
	if (!sd->section_name)
		goto error_mem;

	sd->cmap = malloc(BITFIELD_SIZE(sm->n_options));
	if (!sd->cmap)
		goto error_mem;

	memset(sd->cmap, 0, BITFIELD_SIZE(sm->n_options));
	sd->allocmap[sd->allocmap_len++] = (void *)sd->section_name;
	sd->allocmap[sd->allocmap_len++] = (void *)sd->cmap;

	section = (char *)sd + sizeof(struct uci_sectmap_data);

	err = sm->init_section(map, section, s);
	if (err)
		goto error;

	list_add(&sd->list, &map->sdata);
	err = ucimap_parse_options(map, sm, sd, s);
	if (err)
		goto error;

	err = sm->add_section(map, section);
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
		switch(om->type) {
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


void
ucimap_parse(struct uci_map *map, struct uci_package *pkg)
{
	struct uci_element *e;
	int i;

	uci_foreach_element(&pkg->sections, e) {
		struct uci_section *s = uci_to_section(e);

		for (i = 0; i < map->n_sections; i++) {
			if (strcmp(s->type, map->sections[i].type) != 0)
				continue;
			ucimap_parse_section(map, &map->sections[i], s);
		}
	}
}
