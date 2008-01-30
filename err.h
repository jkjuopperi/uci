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
	int __val = 0;			\
	if (!ctx)			\
		return UCI_ERR_INVAL;	\
	if (!ctx->internal)		\
		__val = setjmp(ctx->trap); \
	ctx->internal = false;		\
	if (__val) {			\
		ctx->errno = __val;	\
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
		ctx->errno = __val;	\
		memcpy(ctx->trap, __old_trap, sizeof(ctx->trap)); \
		goto handler;		\
	}
#define UCI_TRAP_RESTORE(ctx)		\
	memcpy(ctx->trap, __old_trap, sizeof(ctx->trap)); \
} while(0)

#define UCI_INTERNAL(func, ctx, ...) do { \
	ctx->internal = true;		\
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

