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
#include <stdbool.h>
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
enum autoload {
	AUTOLOAD_OFF = 0,
	AUTOLOAD_ON = 1,
	AUTOLOAD_FORCE = 2
};

static struct uci_package *
find_package(lua_State *L, const char *name, enum autoload al)
{
	struct uci_package *p = NULL;
	struct uci_element *e;

	uci_foreach_element(&ctx->root, e) {
		if (strcmp(e->name, name) != 0)
			continue;

		p = uci_to_package(e);
		goto done;
	}

	if (al == AUTOLOAD_FORCE)
		uci_load(ctx, name, &p);
	else if (al) {
		do {
			lua_getfield(L, LUA_GLOBALSINDEX, "uci");
			lua_getfield(L, -1, "autoload");
			if (!lua_isboolean(L, -1))
				break;

			if (!lua_toboolean(L, -1))
				break;

			uci_load(ctx, name, &p);
		} while (0);
		lua_pop(L, 2);
	}

done:
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

static void uci_push_section(lua_State *L, struct uci_section *s)
{
	struct uci_element *e;

	lua_newtable(L);
	lua_pushstring(L, s->type);
	lua_setfield(L, -2, ".TYPE");

	uci_foreach_element(&s->options, e) {
		struct uci_option *o = uci_to_option(e);
		lua_pushstring(L, o->value);
		lua_setfield(L, -2, o->e.name);
	}
}

static void uci_push_package(lua_State *L, struct uci_package *p)
{
	struct uci_element *e;
	int i = 0;

	lua_newtable(L);
	uci_foreach_element(&p->sections, e) {
		i++;
		uci_push_section(L, uci_to_section(e));
		lua_setfield(L, -2, e->name);
	}
}

static int
uci_lua_unload(lua_State *L)
{
	struct uci_package *p;
	const char *s;

	luaL_checkstring(L, 1);
	s = lua_tostring(L, -1);
	p = find_package(L, s, AUTOLOAD_OFF);
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
uci_lua_foreach(lua_State *L)
{
	struct uci_package *p;
	struct uci_element *e;
	const char *package, *type;
	bool ret = false;

	package = luaL_checkstring(L, 1);

	if (lua_isnil(L, 2))
		type = NULL;
	else
		type = luaL_checkstring(L, 2);

	if (!lua_isfunction(L, 3) || !package)
		luaL_error(L, "Invalid argument");

	p = find_package(L, package, AUTOLOAD_ON);
	if (!p)
		goto done;

	uci_foreach_element(&p->sections, e) {
		struct uci_section *s = uci_to_section(e);

		if (type && (strcmp(s->type, type) != 0))
			continue;

		lua_pushvalue(L, 3); /* iterator function */
		uci_push_section(L, s);
		if (lua_pcall(L, 1, 0, 0) == 0)
			ret = true;
	}

done:
	lua_pushboolean(L, ret);
	return 1;
}

static int
uci_lua_get_any(lua_State *L, bool all)
{
	struct uci_element *e = NULL;
	struct uci_package *p = NULL;
	const char *package = NULL;
	const char *section = NULL;
	const char *option = NULL;
	char *s;
	int err = UCI_ERR_MEM;
	int n;

	n = lua_gettop(L);

	luaL_checkstring(L, 1);
	s = strdup(lua_tostring(L, 1));
	if (!s)
		goto error;

	if (n > 1) {
		package = luaL_checkstring(L, 1);
		section = luaL_checkstring(L, 2);
		if (n > 2)
			option = luaL_checkstring(L, 3);
	} else {
		if ((err = uci_parse_tuple(ctx, s, (char **) &package, (char **) &section, (char **) &option, NULL)))
			goto error;
	}

	if (!all && (section == NULL)) {
		err = UCI_ERR_INVAL;
		goto error;
	}

	p = find_package(L, package, AUTOLOAD_ON);
	if (!p) {
		err = UCI_ERR_NOTFOUND;
		goto error;
	}

	if (section) {
		if ((err = uci_lookup(ctx, &e, p, section, option)))
			goto error;
	} else {
		e = &p->e;
	}

	switch(e->type) {
		case UCI_TYPE_PACKAGE:
			uci_push_package(L, p);
			break;
		case UCI_TYPE_SECTION:
			if (all)
				uci_push_section(L, uci_to_section(e));
			else
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
uci_lua_get(lua_State *L)
{
	return uci_lua_get_any(L, false);
}

static int
uci_lua_get_all(lua_State *L)
{
	return uci_lua_get_any(L, true);
}

static int
uci_lua_add(lua_State *L)
{
	struct uci_section *s = NULL;
	struct uci_package *p;
	const char *package;
	const char *type;
	const char *name = NULL;

	do {
		package = luaL_checkstring(L, 1);
		type = luaL_checkstring(L, 2);
		p = find_package(L, package, AUTOLOAD_ON);
		if (!p)
			break;

		if (uci_add_section(ctx, p, type, &s) || !s)
			break;

		name = s->e.name;
	} while (0);

	lua_pushstring(L, name);
	return 1;
}

static int
uci_lua_delete(lua_State *L)
{
	const char *package = NULL;
	const char *section = NULL;
	const char *option = NULL;
	struct uci_package *p;
	const char *s;
	int err = UCI_ERR_MEM;
	int nargs;

	nargs = lua_gettop(L);
	s = luaL_checkstring(L, 1);
	switch(nargs) {
	case 1:
		/* Format: uci.delete("p.s[.o]") */
		s = strdup(s);
		if (!s)
			goto error;

		if ((err = uci_parse_tuple(ctx, (char *) s, (char **) &package, (char **) &section, (char **) &option, NULL)))
			goto error;
		break;
	case 3:
		/* Format: uci.delete("p", "s", "o") */
		option = luaL_checkstring(L, 3);
		/* fall through */
	case 2:
		/* Format: uci.delete("p", "s") */
		section = luaL_checkstring(L, 2);
		package = s;
		break;
	default:
		err = UCI_ERR_INVAL;
		goto error;
	}

	p = find_package(L, package, AUTOLOAD_ON);
	if (!p) {
		err = UCI_ERR_NOTFOUND;
		goto error;
	}
	err = uci_delete(ctx, p, section, option);

error:
	if (err)
		uci_lua_perror(L, "uci.set");
	lua_pushboolean(L, (err == 0));
	return 1;
}

static int
uci_lua_set(lua_State *L)
{
	struct uci_package *p;
	const char *package = NULL;
	const char *section = NULL;
	const char *option = NULL;
	const char *value = NULL;
	const char *s;
	int err = UCI_ERR_MEM;
	int nargs;

	nargs = lua_gettop(L);

	s = luaL_checkstring(L, 1);
	switch(nargs) {
	case 1:
		/* Format: uci.set("p.s.o=v") or uci.set("p.s=v") */
		s = strdup(s);
		if (!s)
			goto error;

		if ((err = uci_parse_tuple(ctx, (char *) s, (char **) &package, (char **) &section, (char **) &option, (char **) &value)))
			goto error;
		break;
	case 4:
		/* Format: uci.set("p", "s", "o", "v") */
		option = luaL_checkstring(L, 3);
		/* fall through */
	case 3:
		/* Format: uci.set("p", "s", "v") */
		package = s;
		section = luaL_checkstring(L, 2);
		value = luaL_checkstring(L, nargs);
		break;
	default:
		err = UCI_ERR_INVAL;
		goto error;
	}

	if ((section == NULL) || (value == NULL)) {
		err = UCI_ERR_INVAL;
		goto error;
	}

	p = find_package(L, package, AUTOLOAD_ON);
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

enum pkg_cmd {
	CMD_SAVE,
	CMD_COMMIT,
	CMD_REVERT
};

static int
uci_lua_package_cmd(lua_State *L, enum pkg_cmd cmd)
{
	struct uci_element *e, *tmp;
	const char *s = NULL;
	const char *section = NULL;
	const char *option = NULL;
	int failed = 0;
	int nargs;

	nargs = lua_gettop(L);
	switch(nargs) {
	case 3:
		if (cmd != CMD_REVERT)
			goto err;
		luaL_checkstring(L, 1);
		option = lua_tostring(L, -1);
		lua_pop(L, 1);
		/* fall through */
	case 2:
		if (cmd != CMD_REVERT)
			goto err;
		luaL_checkstring(L, 1);
		section = lua_tostring(L, -1);
		lua_pop(L, 1);
		/* fall through */
	case 1:
		luaL_checkstring(L, 1);
		s = lua_tostring(L, -1);
		lua_pop(L, 1);
		break;
	case 0:
		break;
	default:
		err:
		luaL_error(L, "Invalid argument count");
		break;
	}

	uci_foreach_element_safe(&ctx->root, tmp, e) {
		struct uci_package *p = uci_to_package(e);
		int ret = UCI_ERR_INVAL;

		if (s && (strcmp(s, e->name) != 0))
			continue;

		switch(cmd) {
		case CMD_COMMIT:
			ret = uci_commit(ctx, &p, false);
			break;
		case CMD_SAVE:
			ret = uci_save(ctx, p);
			break;
		case CMD_REVERT:
			ret = uci_revert(ctx, &p, section, option);
			break;
		}

		if (ret != 0)
			failed = 1;
	}

	lua_pushboolean(L, !failed);
	return 1;
}

static int
uci_lua_save(lua_State *L)
{
	return uci_lua_package_cmd(L, CMD_SAVE);
}

static int
uci_lua_commit(lua_State *L)
{
	return uci_lua_package_cmd(L, CMD_COMMIT);
}

static int
uci_lua_revert(lua_State *L)
{
	return uci_lua_package_cmd(L, CMD_REVERT);
}

static void
uci_lua_add_change(lua_State *L, struct uci_element *e)
{
	struct uci_history *h;
	const char *name;

	h = uci_to_history(e);
	if (!h->section)
		return;

	lua_getfield(L, -1, h->section);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1); /* copy for setfield */
		lua_setfield(L, -3, h->section);
	}

	name = (h->e.name ? h->e.name : ".TYPE");
	if (h->value)
		lua_pushstring(L, h->value);
	else
		lua_pushstring(L, "");
	lua_setfield(L, -2, name);
	lua_pop(L, 1);
}

static void
uci_lua_changes_pkg(lua_State *L, const char *package)
{
	struct uci_package *p = NULL;
	struct uci_element *e;
	bool autoload = false;

	p = find_package(L, package, AUTOLOAD_OFF);
	if (!p) {
		autoload = true;
		p = find_package(L, package, AUTOLOAD_FORCE);
		if (!p)
			return;
	}

	if (uci_list_empty(&p->history) && uci_list_empty(&p->saved_history))
		goto done;

	lua_newtable(L);
	uci_foreach_element(&p->saved_history, e) {
		uci_lua_add_change(L, e);
	}
	uci_foreach_element(&p->history, e) {
		uci_lua_add_change(L, e);
	}
	lua_setfield(L, -2, p->e.name);

done:
	if (autoload)
		uci_unload(ctx, p);
}

static int
uci_lua_changes(lua_State *L)
{
	const char *package = NULL;
	char **config = NULL;
	int nargs;
	int i;

	nargs = lua_gettop(L);
	switch(nargs) {
	case 1:
		package = luaL_checkstring(L, 1);
	case 0:
		break;
	default:
		luaL_error(L, "invalid argument count");
	}

	lua_newtable(L);
	if (package) {
		uci_lua_changes_pkg(L, package);
	} else {
		if (uci_list_configs(ctx, &config) != 0)
			goto done;

		for(i = 0; config[i] != NULL; i++) {
			uci_lua_changes_pkg(L, config[i]);
		}
	}

done:
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
	{ "get_all", uci_lua_get_all },
	{ "add", uci_lua_add },
	{ "set", uci_lua_set },
	{ "save", uci_lua_save },
	{ "delete", uci_lua_delete },
	{ "commit", uci_lua_commit },
	{ "revert", uci_lua_revert },
	{ "changes", uci_lua_changes },
	{ "foreach", uci_lua_foreach },
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

	/* enable autoload by default */
	lua_getfield(L, LUA_GLOBALSINDEX, "uci");
	lua_pushboolean(L, 1);
	lua_setfield(L, -2, "autoload");
	lua_pop(L, 1);

	return 0;
}
