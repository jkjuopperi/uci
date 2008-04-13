/*
 * libuci plugin for Lua
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

#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#include <lauxlib.h>
#include <uci.h>

#define MODNAME        "uci"
//#define DEBUG 1

#ifdef DEBUG
#define DPRINTF(...) fprintf(stderr, __VA_ARGS__)
#else
#define DPRINTF(...) do {} while (0)
#endif

static struct uci_context *ctx = NULL;

static struct uci_package *
find_package(const char *name)
{
	struct uci_package *p = NULL;
	struct uci_element *e;
	uci_foreach_element(&ctx->root, e) {
		if (strcmp(e->name, name) != 0)
			continue;

		p = uci_to_package(e);
		break;
	}
	return p;
}

static void uci_lua_perror(lua_State *L, char *name)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "uci");
	lua_getfield(L, -1, "warn");
	if (!lua_isboolean(L, -1))
		goto done;
	if (lua_toboolean(L, -1) != 1)
		goto done;
	uci_perror(ctx, name);
done:
	lua_pop(L, 2);
}

static int
uci_lua_unload(lua_State *L)
{
	struct uci_package *p;
	const char *s;

	luaL_checkstring(L, 1);
	s = lua_tostring(L, -1);
	p = find_package(s);
	if (p) {
		uci_unload(ctx, p);
		lua_pushboolean(L, 1);
	} else {
		lua_pushboolean(L, 0);
	}
	return 1;
}

static int
uci_lua_load(lua_State *L)
{
	struct uci_package *p = NULL;
	const char *s;

	uci_lua_unload(L);
	lua_pop(L, 1); /* bool ret value of unload */
	s = lua_tostring(L, -1);

	if (uci_load(ctx, s, &p)) {
		uci_lua_perror(L, "uci.load");
		lua_pushboolean(L, 0);
	} else {
		lua_pushboolean(L, 1);
	}

	return 1;
}

static int
uci_lua_get(lua_State *L)
{
	struct uci_lua_context *f;
	struct uci_element *e = NULL;
	struct uci_package *p = NULL;
	char *package = NULL;
	char *section = NULL;
	char *option = NULL;
	char *s;
	int err = UCI_ERR_MEM;

	luaL_checkstring(L, 1);
	s = strdup(lua_tostring(L, -1));
	if (!s)
		goto error;

	if ((err = uci_parse_tuple(ctx, s, &package, &section, &option, NULL)))
		goto error;

	if (section == NULL) {
		err = UCI_ERR_INVAL;
		goto error;
	}

	p = find_package(package);
	if (!p) {
		err = UCI_ERR_NOTFOUND;
		goto error;
	}

	if ((err = uci_lookup(ctx, &e, p, section, option)))
		goto error;

	switch(e->type) {
		case UCI_TYPE_SECTION:
			lua_pushstring(L, uci_to_section(e)->type);
			break;
		case UCI_TYPE_OPTION:
			lua_pushstring(L, uci_to_option(e)->value);
			break;
		default:
			err = UCI_ERR_INVAL;
			goto error;
	}
error:
	if (s)
		free(s);

	switch(err) {
	default:
		ctx->err = err;
		uci_lua_perror(L, "uci.get");
		/* fall through */
	case UCI_ERR_NOTFOUND:
		lua_pushnil(L);
		/* fall through */
	case 0:
		return 1;
	}
}


static int
uci_lua_set(lua_State *L)
{
	struct uci_package *p;
	char *package = NULL;
	char *section = NULL;
	char *option = NULL;
	char *value = NULL;
	char *s;
	int err = UCI_ERR_MEM;

	luaL_checkstring(L, 1);
	s = strdup(lua_tostring(L, -1));
	if (!s)
		goto error;

	if ((err = uci_parse_tuple(ctx, s, &package, &section, &option, &value)))
		goto error;

	if ((section == NULL) || (value == NULL)) {
		err = UCI_ERR_INVAL;
		goto error;
	}

	p = find_package(package);
	if (!p) {
		err = UCI_ERR_NOTFOUND;
		goto error;
	}
	err = uci_set(ctx, p, section, option, value, NULL);

error:
	if (err)
		uci_lua_perror(L, "uci.set");
	lua_pushboolean(L, (err == 0));
	return 1;
}

static int
uci_lua_commit(lua_State *L)
{
	struct uci_element *e, *tmp;
	const char *s = NULL;
	int failed = 0;

	if (!lua_isnoneornil(L, -1)) {
		luaL_checkstring(L, 1);
		s = lua_tostring(L, -1);
	}

	uci_foreach_element_safe(&ctx->root, tmp, e) {
		struct uci_package *p = uci_to_package(e);

		if (s && (strcmp(s, e->name) != 0))
			continue;

		if (uci_commit(ctx, &p, false) != 0)
			failed = 1;
	}
	lua_pushboolean(L, !failed);
	return 1;
}

static int
uci_lua_save(lua_State *L)
{
	struct uci_element *e;
	const char *s = NULL;
	int failed = 0;

	if (!lua_isnoneornil(L, -1)) {
		luaL_checkstring(L, 1);
		s = lua_tostring(L, -1);
	}

	uci_foreach_element(&ctx->root, e) {
		if (s && (strcmp(s, e->name) != 0))
			continue;

		if (uci_save(ctx, uci_to_package(e)) != 0)
			failed = 1;
	}
	lua_pushboolean(L, !failed);
	return 1;
}

static int
uci_lua_set_confdir(lua_State *L)
{
	int ret;

	luaL_checkstring(L, 1);
	ret = uci_set_confdir(ctx, lua_tostring(L, -1));
	lua_pushboolean(L, (ret == 0));
	return 1;
}

static int
uci_lua_set_savedir(lua_State *L)
{
	int ret;

	luaL_checkstring(L, 1);
	ret = uci_set_savedir(ctx, lua_tostring(L, -1));
	lua_pushboolean(L, (ret == 0));

	return 1;
}

static const luaL_Reg uci[] = {
	{ "load", uci_lua_load },
	{ "unload", uci_lua_unload },
	{ "get", uci_lua_get },
	{ "set", uci_lua_set },
	{ "save", uci_lua_save },
	{ "commit", uci_lua_commit },
	{ "set_confdir", uci_lua_set_confdir },
	{ "set_savedir", uci_lua_set_savedir },
	{ NULL, NULL },
};


int
luaopen_uci(lua_State *L)
{
	ctx = uci_alloc_context();
	if (!ctx)
		luaL_error(L, "Cannot allocate UCI context\n");
	luaL_register(L, MODNAME, uci);
	return 0;
}
