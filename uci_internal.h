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

#ifndef __UCI_INTERNAL_H
#define __UCI_INTERNAL_H

#define __public
#ifdef UCI_PLUGIN_SUPPORT
#define __plugin extern
#else
#define __plugin static
#endif

struct uci_parse_context
{
	/* error context */
	const char *reason;
	int line;
	int byte;

	/* private: */
	struct uci_package *package;
	struct uci_section *section;
	bool merge;
	FILE *file;
	const char *name;
	char *buf;
	int bufsz;
};

__plugin void *uci_malloc(struct uci_context *ctx, size_t size);
__plugin void *uci_realloc(struct uci_context *ctx, void *ptr, size_t size);
__plugin char *uci_strdup(struct uci_context *ctx, const char *str);
__plugin bool uci_validate_str(const char *str, bool name);
__plugin void uci_add_history(struct uci_context *ctx, struct uci_list *list, int cmd, const char *section, const char *option, const char *value);
__plugin void uci_free_history(struct uci_history *h);
__plugin struct uci_package *uci_alloc_package(struct uci_context *ctx, const char *name);

#ifdef UCI_PLUGIN_SUPPORT
/**
 * uci_add_backend: add an extra backend
 * @ctx: uci context
 * @name: name of the backend
 *
 * The default backend is "file", which uses /etc/config for config storage
 */
__plugin int uci_add_backend(struct uci_context *ctx, struct uci_backend *b);

/**
 * uci_add_backend: add an extra backend
 * @ctx: uci context
 * @name: name of the backend
 *
 * The default backend is "file", which uses /etc/config for config storage
 */
__plugin int uci_del_backend(struct uci_context *ctx, struct uci_backend *b);
#endif

#define UCI_BACKEND(_var, _name, ...)	\
struct uci_backend _var = {		\
	.e.list = {			\
		.next = &_var.e.list,	\
		.prev = &_var.e.list,	\
	},				\
	.e.name = _name,		\
	.e.type = UCI_TYPE_BACKEND,	\
	.ptr = &_var,			\
	__VA_ARGS__			\
}


/*
 * functions for debug and error handling, for internal use only
 */

#ifdef UCI_DEBUG
#define DPRINTF(...) fprintf(stderr, __VA_ARGS__)
#else
#define DPRINTF(...)
#endif

/* 
 * throw an uci exception and store the error number
 * in the context.
 */
#define UCI_THROW(ctx, err) do { 	\
	DPRINTF("Exception: %s in %s, %s:%d\n", #err, __func__, __FILE__, __LINE__); \
	longjmp(ctx->trap, err); 	\
} while (0)

/*
 * store the return address for handling exceptions
 * needs to be called in every externally visible library function
 *
 * NB: this does not handle recursion at all. Calling externally visible
 * functions from other uci functions is only allowed at the end of the
 * calling function, or by wrapping the function call in UCI_TRAP_SAVE
 * and UCI_TRAP_RESTORE.
 */
#define UCI_HANDLE_ERR(ctx) do {	\
	DPRINTF("ENTER: %s\n", __func__); \
	int __val = 0;			\
	ctx->err = 0;			\
	if (!ctx)			\
		return UCI_ERR_INVAL;	\
	if (!ctx->internal && !ctx->nested) \
		__val = setjmp(ctx->trap); \
	ctx->internal = false;		\
	ctx->nested = false;		\
	if (__val) {			\
		DPRINTF("LEAVE: %s, ret=%d\n", __func__, __val); \
		ctx->err = __val;	\
		return __val;		\
	}				\
} while (0)

/*
 * In a block enclosed by UCI_TRAP_SAVE and UCI_TRAP_RESTORE, all exceptions
 * are intercepted and redirected to the label specified in 'handler'
 * after UCI_TRAP_RESTORE, or when reaching the 'handler' label, the old
 * exception handler is restored
 */
#define UCI_TRAP_SAVE(ctx, handler) do {   \
	jmp_buf	__old_trap;		\
	int __val;			\
	memcpy(__old_trap, ctx->trap, sizeof(ctx->trap)); \
	__val = setjmp(ctx->trap);	\
	if (__val) {			\
		ctx->err = __val;	\
		memcpy(ctx->trap, __old_trap, sizeof(ctx->trap)); \
		goto handler;		\
	}
#define UCI_TRAP_RESTORE(ctx)		\
	memcpy(ctx->trap, __old_trap, sizeof(ctx->trap)); \
} while(0)

/**
 * UCI_INTERNAL: Do an internal call of a public API function
 * 
 * Sets Exception handling to passthrough mode.
 * Allows API functions to change behavior compared to public use
 */
#define UCI_INTERNAL(func, ctx, ...) do { \
	ctx->internal = true;		\
	func(ctx, __VA_ARGS__);		\
} while (0)

/**
 * UCI_NESTED: Do an normal nested call of a public API function
 * 
 * Sets Exception handling to passthrough mode.
 * Allows API functions to change behavior compared to public use
 */
#define UCI_NESTED(func, ctx, ...) do { \
	ctx->nested = true;		\
	func(ctx, __VA_ARGS__);		\
} while (0)


/*
 * check the specified condition.
 * throw an invalid argument exception if it's false
 */
#define UCI_ASSERT(ctx, expr) do {	\
	if (!(expr)) {			\
		DPRINTF("[%s:%d] Assertion failed\n", __FILE__, __LINE__); \
		UCI_THROW(ctx, UCI_ERR_INVAL);	\
	}				\
} while (0)

#endif
