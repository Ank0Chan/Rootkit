// SPDX-License-Identifier: GPL-2.0
/*
 * companion_dev.c - Command channel of segment B.
 *
 * Exposes a misc character device named "companion". This device is the
 * single communication endpoint between the userland companion program
 * (segment D) and the kernel core. The wire format is defined in
 * include/protocol.h.
 *
 * At the J1 milestone the file operations are deliberately skeletons:
 * they establish the device lifecycle and the entry points, but the
 * command protocol is not yet decoded. See the TODO markers below.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

#include "../include/protocol.h"

/* Name under which the device appears in /dev. */
#define COMPANION_DEV_NAME "companion"

/*
 * open - called when a process opens /dev/companion.
 *
 * Returns 0 on success. No per-open state is allocated yet.
 */
static int companion_open(struct inode *inode, struct file *file)
{
	pr_info("companion: open\n");
	return 0;
}

/*
 * release - called when the last reference to the device is closed.
 */
static int companion_release(struct inode *inode, struct file *file)
{
	pr_info("companion: release\n");
	return 0;
}

/*
 * read - deliver a response to the companion.
 *
 * Skeleton: reports end-of-file (0 bytes) until the protocol is wired up.
 */
static ssize_t companion_read(struct file *file, char __user *buf,
			      size_t len, loff_t *off)
{
	pr_info("companion: read (len=%zu)\n", len);
	return 0;
}

/*
 * write - receive a command from the companion.
 *
 * Skeleton: acknowledges every byte as consumed so the companion does not
 * spin, but performs no decoding yet.
 */
static ssize_t companion_write(struct file *file, const char __user *buf,
			       size_t len, loff_t *off)
{
	pr_info("companion: write (len=%zu)\n", len);

	/*
	 * TODO (protocol): copy the incoming bytes into a bounded kernel
	 * buffer, validate that len >= sizeof(struct cmd_request) and that
	 * arg_len is consistent with len, then dispatch on cmd_request.command
	 * and stage a struct cmd_response for the next read(). Reject
	 * oversized or malformed requests with -EINVAL. Never trust len or
	 * arg_len without bounds checks.
	 */
	return len;
}

/*
 * unlocked_ioctl - alternative control path for the companion.
 *
 * Skeleton: no command is recognized yet.
 */
static long companion_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	pr_info("companion: ioctl (cmd=0x%x)\n", cmd);

	/*
	 * TODO (protocol): define the ioctl command space (magic number,
	 * _IOW/_IOR encodings) and dispatch here. Until then, signal that no
	 * ioctl is supported so callers fall back cleanly.
	 */
	return -ENOTTY;
}

/* Operations table bound to the misc device. */
static const struct file_operations companion_fops = {
	.owner          = THIS_MODULE,
	.open           = companion_open,
	.release        = companion_release,
	.read           = companion_read,
	.write          = companion_write,
	.unlocked_ioctl = companion_ioctl,
};

/*
 * Misc device descriptor.
 *
 * MISC_DYNAMIC_MINOR lets the kernel assign a free minor number. Mode 0600
 * restricts the node to its owner (root), which is appropriate for a
 * privileged control channel.
 */
static struct miscdevice companion_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = COMPANION_DEV_NAME,
	.fops  = &companion_fops,
	.mode  = 0600,
};

/*
 * companion_dev_register - create /dev/companion.
 *
 * Returns 0 on success or a negative errno on failure. Called from the
 * module init path.
 */
int companion_dev_register(void)
{
	int ret;

	ret = misc_register(&companion_misc);
	if (ret) {
		pr_err("companion: misc_register failed (%d)\n", ret);
		return ret;
	}

	pr_info("companion: registered as /dev/%s (minor %d)\n",
		COMPANION_DEV_NAME, companion_misc.minor);
	return 0;
}

/*
 * companion_dev_unregister - remove /dev/companion.
 *
 * Idempotent with respect to a successful register; called from the module
 * exit path.
 */
void companion_dev_unregister(void)
{
	misc_deregister(&companion_misc);
	pr_info("companion: unregistered\n");
}
