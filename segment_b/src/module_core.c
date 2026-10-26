// SPDX-License-Identifier: GPL-2.0
/*
 * module_core.c - Entry point of segment B.
 *
 * Owns the module lifecycle: it brings up the command channel (companion
 * misc device) and, optionally, an inert self-test of the syscall hooking
 * framework. Any resource acquired in init is released in exit, using a
 * cascading goto error path so partial initialization never leaks.
 *
 * The self-test is disabled by default (selftest=0): loaded on its own,
 * segment B changes no system behaviour. Segment C provides the real hook
 * tables (e.g. getdents64) on top of the framework in syscall_hook.c. The
 * kill syscall must never be hooked.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/ptrace.h>
#include <linux/linkage.h>

#include "../include/companion_dev.h"
#include "../include/syscall_hook.h"

/* ------------------------------------------------------------------ *
 *  Hooking self-test (optional, inert)
 * ------------------------------------------------------------------ */

/*
 * selftest - when true, install the demonstration hook below at load time.
 * It only proves the ftrace mechanism works end to end; it changes no return
 * value. Read-only module parameter: set with `insmod rootkit_b.ko selftest=1`.
 */
static bool selftest;
module_param(selftest, bool, 0444);
MODULE_PARM_DESC(selftest,
		 "Install an inert demonstration syscall hook (default off)");

/* Set once the hooks are installed, so exit only removes what init added. */
static bool hooks_installed;

/* Number of times the demonstration hook has run. */
static atomic_t selftest_calls = ATOMIC_INIT(0);

/* Original __x64_sys_getpid, filled by the hooking framework. */
static asmlinkage long (*orig_getpid)(const struct pt_regs *regs);

/*
 * selftest_getpid - transparent replacement of getpid.
 *
 * Counts the call, then forwards to the original and returns its value
 * unchanged. This demonstrates "replacing an existing syscall" with zero
 * behavioural impact, keeping the system stable.
 */
static asmlinkage long selftest_getpid(const struct pt_regs *regs)
{
	atomic_inc(&selftest_calls);
	return orig_getpid(regs);
}

static struct ftrace_hook selftest_hooks[] = {
	HOOK("__x64_sys_getpid", selftest_getpid, &orig_getpid),
};

/* ------------------------------------------------------------------ *
 *  Module lifecycle
 * ------------------------------------------------------------------ */

/*
 * rootkit_b_init - module load path.
 *
 * Acquires resources in order; on failure, unwinds in reverse from the
 * matching label.
 */
static int __init rootkit_b_init(void)
{
	int ret;

	pr_info("rootkit_b: init\n");

	ret = companion_dev_register();
	if (ret)
		goto err_companion;

	if (selftest) {
		ret = fh_install_hooks(selftest_hooks,
				       ARRAY_SIZE(selftest_hooks));
		if (ret)
			goto err_hooks;
		hooks_installed = true;
		pr_info("rootkit_b: self-test hook installed\n");
	}

	pr_info("rootkit_b: ready\n");
	return 0;

err_hooks:
	companion_dev_unregister();
err_companion:
	pr_err("rootkit_b: init failed (%d)\n", ret);
	return ret;
}

/*
 * rootkit_b_exit - module unload path.
 *
 * Releases resources in the reverse order of init.
 */
static void __exit rootkit_b_exit(void)
{
	pr_info("rootkit_b: exit\n");

	if (hooks_installed) {
		fh_remove_hooks(selftest_hooks, ARRAY_SIZE(selftest_hooks));
		pr_info("rootkit_b: self-test hook removed (%d calls)\n",
			atomic_read(&selftest_calls));
	}

	companion_dev_unregister();
	pr_info("rootkit_b: unloaded\n");
}

module_init(rootkit_b_init);
module_exit(rootkit_b_exit);

/* ------------------------------------------------------------------ *
 *  Module metadata
 * ------------------------------------------------------------------ */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Segment B");
MODULE_DESCRIPTION("Rootkit coursework - segment B: kernel core and command channel");
MODULE_VERSION("0.1.0");
