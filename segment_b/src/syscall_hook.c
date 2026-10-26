// SPDX-License-Identifier: GPL-2.0
/*
 * syscall_hook.c - ftrace-based function replacement framework (segment B).
 *
 * Mechanism (per TP03):
 *   - resolve symbols with kallsyms_lookup_name, itself obtained through a
 *     kprobe (kallsyms_lookup_name is no longer exported to modules);
 *   - redirect each target function with ftrace, using FTRACE_OPS_FL_SAVE_REGS
 *     and FTRACE_OPS_FL_IPMODIFY so the callback can rewrite the instruction
 *     pointer to the replacement function;
 *   - a within_module() guard prevents infinite recursion when the
 *     replacement calls back into the original.
 *
 * Kernel config required on the target: CONFIG_KPROBES, CONFIG_FUNCTION_TRACER,
 * CONFIG_DYNAMIC_FTRACE, CONFIG_DYNAMIC_FTRACE_WITH_REGS, CONFIG_KALLSYMS.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ftrace.h>
#include <linux/kprobes.h>
#include <linux/slab.h>

#include "../include/syscall_hook.h"

/* ------------------------------------------------------------------ *
 *  Symbol resolution
 * ------------------------------------------------------------------ */

/* Resolved once, then reused for every hook. */
static unsigned long (*fh_kallsyms_lookup_name)(const char *name);

/*
 * fh_resolve_kallsyms - obtain kallsyms_lookup_name via a short-lived kprobe.
 *
 * Returns 0 on success or a negative errno. Idempotent.
 */
static int fh_resolve_kallsyms(void)
{
	static struct kprobe kp = { .symbol_name = "kallsyms_lookup_name" };
	int ret;

	if (fh_kallsyms_lookup_name)
		return 0;

	ret = register_kprobe(&kp);
	if (ret < 0) {
		pr_err("syscall_hook: register_kprobe failed (%d)\n", ret);
		return ret;
	}

	fh_kallsyms_lookup_name = (void *)kp.addr;
	unregister_kprobe(&kp);

	if (!fh_kallsyms_lookup_name) {
		pr_err("syscall_hook: kallsyms_lookup_name not found\n");
		return -ENOENT;
	}

	return 0;
}

/*
 * fh_resolve_hook_address - fill hook->address and the caller's original
 *                           function pointer.
 */
static int fh_resolve_hook_address(struct ftrace_hook *hook)
{
	hook->address = fh_kallsyms_lookup_name(hook->name);
	if (!hook->address) {
		pr_err("syscall_hook: unresolved symbol: %s\n", hook->name);
		return -ENOENT;
	}

	*((unsigned long *)hook->original) = hook->address;
	return 0;
}

/* ------------------------------------------------------------------ *
 *  ftrace callback
 * ------------------------------------------------------------------ */

/*
 * fh_ftrace_thunk - called by ftrace on entry to a hooked function.
 *
 * Rewrites the saved instruction pointer to the replacement function, except
 * when the call originates from this module: that is the replacement calling
 * the original, which must proceed unredirected (anti-recursion guard).
 *
 * The 4th parameter is struct ftrace_regs * on kernels >= 5.11; pt_regs is
 * extracted with ftrace_get_regs(), valid because FTRACE_OPS_FL_SAVE_REGS is
 * set on the ops.
 */
static void notrace fh_ftrace_thunk(unsigned long ip, unsigned long parent_ip,
				    struct ftrace_ops *ops,
				    struct ftrace_regs *fregs)
{
	struct ftrace_hook *hook = container_of(ops, struct ftrace_hook, ops);
	struct pt_regs *regs = ftrace_get_regs(fregs);

	if (!regs)
		return;

	if (!within_module(parent_ip, THIS_MODULE))
		regs->ip = (unsigned long)hook->function;
}

/* ------------------------------------------------------------------ *
 *  Install / remove
 * ------------------------------------------------------------------ */

static int fh_install_hook(struct ftrace_hook *hook)
{
	int ret;

	ret = fh_resolve_hook_address(hook);
	if (ret)
		return ret;

	hook->ops.func = fh_ftrace_thunk;
	hook->ops.flags = FTRACE_OPS_FL_SAVE_REGS | FTRACE_OPS_FL_IPMODIFY;

	ret = ftrace_set_filter_ip(&hook->ops, hook->address, 0, 0);
	if (ret) {
		pr_err("syscall_hook: ftrace_set_filter_ip(%s) failed (%d)\n",
		       hook->name, ret);
		return ret;
	}

	ret = register_ftrace_function(&hook->ops);
	if (ret) {
		pr_err("syscall_hook: register_ftrace_function(%s) failed (%d)\n",
		       hook->name, ret);
		ftrace_set_filter_ip(&hook->ops, hook->address, 1, 0);
		return ret;
	}

	pr_info("syscall_hook: hooked %s\n", hook->name);
	return 0;
}

static void fh_remove_hook(struct ftrace_hook *hook)
{
	int ret;

	ret = unregister_ftrace_function(&hook->ops);
	if (ret)
		pr_err("syscall_hook: unregister_ftrace_function(%s) failed (%d)\n",
		       hook->name, ret);

	ret = ftrace_set_filter_ip(&hook->ops, hook->address, 1, 0);
	if (ret)
		pr_err("syscall_hook: ftrace_set_filter_ip(%s) remove failed (%d)\n",
		       hook->name, ret);
}

int fh_install_hooks(struct ftrace_hook *hooks, size_t count)
{
	int ret;
	size_t i;

	ret = fh_resolve_kallsyms();
	if (ret)
		return ret;

	for (i = 0; i < count; i++) {
		ret = fh_install_hook(&hooks[i]);
		if (ret)
			goto err_unwind;
	}

	return 0;

err_unwind:
	/* Roll back the hooks installed so far, in reverse order. */
	while (i > 0)
		fh_remove_hook(&hooks[--i]);

	return ret;
}

void fh_remove_hooks(struct ftrace_hook *hooks, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		fh_remove_hook(&hooks[i]);
}
