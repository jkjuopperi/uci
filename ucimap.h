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
#include <stdbool.h>
#include "uci_list.h"
#include "uci.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#define BITFIELD_SIZE(_fields) (((_fields) / 8) + 1)

#define CLR_BIT(_name, _bit) do { \
	_name[(_bit) / 8] &= ~(1 << ((_bit) % 8)); \
} while (0)

#define SET_BIT(_name, _bit) do { \
	_name[(_bit) / 8] |=  (1 << ((_bit) % 8)); \
} while (0)

#define TEST_BIT(_name, _bit) \
	(_name[(_bit) / 8] & (1 << ((_bit) % 8)))

#define UCIMAP_OPTION(_type, _field) \
	.type = UCIMAP_CUSTOM, \
	.name = #_field, \
	.offset = offsetof(_type, _field)


#define UCIMAP_SECTION(_name, _field) \
	.alloc_len = sizeof(_name), \
	.smap_offset = offsetof(_name, _field)

struct uci_sectionmap;
struct uci_optmap;
struct ucimap_list;

struct uci_map {
	struct uci_sectionmap **sections;
	unsigned int n_sections;
	struct list_head sdata;
	struct list_head fixup;

	void *priv; /* user data */
};

enum ucimap_type {
	/* types */
	UCIMAP_SIMPLE   = 0x00,
	UCIMAP_LIST     = 0x10,
	UCIMAP_TYPE     = 0xf0, /* type mask */

	/* subtypes */
	UCIMAP_STRING   = 0x0,
	UCIMAP_BOOL     = 0x1,
	UCIMAP_INT      = 0x2,
	UCIMAP_SECTION  = 0x3,
	UCIMAP_CUSTOM	= 0x4,
	UCIMAP_SUBTYPE  = 0xf, /* subtype mask */

	/* automatically create lists from
	 * options with space-separated items */
	UCIMAP_LIST_AUTO = 0x0100,
	UCIMAP_FLAGS     = 0xff00, /* flags mask */
};

union ucimap_data {
	int i;
	bool b;
	char *s;
	void *section;
	struct ucimap_list *list;
};

struct ucimap_section_data {
	struct list_head list;
	struct uci_map *map;
	struct uci_sectionmap *sm;
	const char *section_name;

	/* list of allocations done by ucimap */
	struct uci_alloc *allocmap;
	unsigned long allocmap_len;

	/* map for changed fields */
	unsigned char *cmap;
	bool done;
};


struct uci_listmap {
	struct list_head list;
	union ucimap_data data;
};

struct uci_sectionmap {
	/* type string for the uci section */
	const char *type;

	/* length of the struct to map into, filled in by macro */
	unsigned int alloc_len;

	/* sectionmap offset, filled in by macro */
	unsigned int smap_offset;

	/* return a pointer to the section map data (allocate if necessary) */
	struct ucimap_section_data *(*alloc)(struct uci_map *map,
		struct uci_sectionmap *sm, struct uci_section *s);

	/* give the caller time to initialize the preallocated struct */
	int (*init)(struct uci_map *map, void *section, struct uci_section *s);

	/* pass the fully processed struct to the callback after the section end */
	int (*add)(struct uci_map *map, void *section);

	/* let the callback clean up its own stuff in the section */
	int (*free)(struct uci_map *map, void *section);

	/* list of option mappings for this section */
	struct uci_optmap *options;
	unsigned int n_options;
	unsigned int options_size;
};

struct uci_optmap {
	unsigned int offset;
	const char *name;
	enum ucimap_type type;
	int (*parse)(void *section, struct uci_optmap *om, union ucimap_data *data, const char *string);
	int (*format)(void *section, struct uci_optmap *om, union ucimap_data *data, char **string);
	union {
		struct {
			int base;
			int min;
			int max;
		} i;
		struct {
			int maxlen;
		} s;
		struct uci_sectionmap *sm;
	} data;
};

struct ucimap_list {
	int n_items;
	union ucimap_data item[];
};

extern int ucimap_init(struct uci_map *map);
extern void ucimap_cleanup(struct uci_map *map);
extern void ucimap_set_changed(struct ucimap_section_data *sd, void *field);
extern int ucimap_store_section(struct uci_map *map, struct uci_package *p, struct ucimap_section_data *sd);
extern void ucimap_parse(struct uci_map *map, struct uci_package *pkg);

