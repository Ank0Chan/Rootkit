// SPDX-License-Identifier: GPL-2.0
/*
 * module_core.c - Entry point of segment B.
 *
 * Owns the module lifecycle. Its only responsibility at J1 is to bring up
 * and tear down the command channel (companion misc device) cleanly. Any
 * resource acquired in init is released in exit, using a cascading goto
 * error path so partial initialization never leaks.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include "../include/companion_dev.h"

/* ------------------------------------------------------------------ *
 *  Module lifecycle
 * ------------------------------------------------------------------ */

/*
 * rootkit_b_init - module load path.
 *
 * The goto cascade is overkill for a single resource today, but it is the
 * pattern every later resource must slot into: acquire in order, and on
 * failure unwind in reverse from the matching label.
 */
static int __init rootkit_b_init(void)
{
	int ret;

	pr_info("rootkit_b: init\n");

	ret = companion_dev_register();
	if (ret)
		goto err_companion;

	pr_info("rootkit_b: ready\n");
	return 0;

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
