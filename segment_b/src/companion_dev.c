// SPDX-License-Identifier: GPL-2.0
/*
 * companion_dev.c - Command channel of segment B.
 *
 * Exposes a misc character device named "companion". This device is the
 * single communication endpoint between the userland companion program
 * (segment D) and the kernel core. The wire format is defined in
 * include/protocol.h.
 *
 * Usage contract (unchanged since J1):
 *   - the companion writes one serialized struct cmd_request per command;
 *   - it then reads the matching serialized struct cmd_response.
 *
 * The write() path validates and dispatches the request; the response it
 * builds is staged per open file and delivered by the next read().
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/bitops.h>
#include <linux/string.h>
#include <linux/minmax.h>
#include <linux/uaccess.h>

#include "../include/protocol.h"
#include "../include/companion_dev.h"

/* Name under which the device appears in /dev. */
#define COMPANION_DEV_NAME "companion"

/*
 * Implementation limits (not part of the wire contract).
 *
 * They bound every allocation made from a user-supplied length so a
 * malformed or hostile request can never exhaust kernel memory.
 */
#define COMPANION_MAX_PAYLOAD 4096u
#define COMPANION_MAX_REQUEST (sizeof(struct cmd_request) + COMPANION_MAX_PAYLOAD)
#define COMPANION_MAX_DATA    4096u

/*
 * Version string returned by CMD_GET_VERSION. Kept in sync with the
 * MODULE_VERSION declared in module_core.c.
 */
#define COMPANION_VERSION_STRING "0.1.0"

/*
 * Runtime flags manipulated by CMD_SET_FLAG / CMD_CLEAR_FLAG.
 *
 * Bit operations on this word are atomic (set_bit/clear_bit), so no extra
 * lock is needed to serialize concurrent flag changes.
 */
static unsigned long companion_flags;

/*
 * Per-open session state.
 *
 * Holds the serialized response staged by the last write() so the next
 * read() can deliver it, possibly across several partial reads. The mutex
 * serializes write() (which replaces the response) against read() (which
 * consumes it) on the same open file.
 */
struct companion_session {
	struct mutex lock;   /* guards resp / resp_len / resp_read       */
	u8 *resp;            /* serialized struct cmd_response, or NULL  */
	size_t resp_len;     /* total valid bytes in resp                */
	size_t resp_read;    /* bytes already delivered by read()        */
};

/* ------------------------------------------------------------------ *
 *  Response staging
 * ------------------------------------------------------------------ */

/*
 * stage_response - build a serialized cmd_response and attach it to the
 *                  session, replacing any previously staged one.
 *
 * Returns 0 on success or -ENOMEM on allocation failure. Caller must hold
 * session->lock.
 */
static int stage_response(struct companion_session *session, s32 status,
			  const void *data, u32 data_len)
{
	struct cmd_response *resp;
	size_t total;

	if (data_len > COMPANION_MAX_DATA)
		return -EINVAL;

	total = sizeof(*resp) + data_len;

	resp = kmalloc(total, GFP_KERNEL);
	if (!resp)
		return -ENOMEM;

	resp->status = status;
	resp->data_len = data_len;
	if (data_len)
		memcpy(resp->data, data, data_len);

	/* Discard a response the reader never consumed. */
	kfree(session->resp);
	session->resp = (u8 *)resp;
	session->resp_len = total;
	session->resp_read = 0;

	return 0;
}

/* ------------------------------------------------------------------ *
 *  Command handlers
 * ------------------------------------------------------------------ */

/*
 * handle_set_flag - set or clear a runtime flag bit.
 *
 * The payload must carry exactly one u32 flag index in [0, BITS_PER_LONG).
 * Returns a CMD_STATUS_* code to report in the response.
 */
static s32 handle_set_flag(const u8 *payload, u32 arg_len, bool set)
{
	u32 flag_id;

	if (arg_len != sizeof(flag_id))
		return CMD_STATUS_ERR;

	memcpy(&flag_id, payload, sizeof(flag_id));
	if (flag_id >= BITS_PER_LONG)
		return CMD_STATUS_ERR;

	if (set)
		set_bit(flag_id, &companion_flags);
	else
		clear_bit(flag_id, &companion_flags);

	return CMD_STATUS_OK;
}

/*
 * dispatch_command - route a validated request to its handler and stage
 *                    the resulting response.
 *
 * Returns 0 once a response has been staged (including error responses),
 * or a negative errno if the response could not be built. Caller must hold
 * session->lock.
 */
static int dispatch_command(struct companion_session *session,
			    const struct cmd_request *req)
{
	switch (req->command) {
	case CMD_PING:
		return stage_response(session, CMD_STATUS_OK, NULL, 0);

	case CMD_GET_VERSION:
		return stage_response(session, CMD_STATUS_OK,
				      COMPANION_VERSION_STRING,
				      (u32)strlen(COMPANION_VERSION_STRING));

	case CMD_SET_FLAG: {
		s32 status = handle_set_flag(req->payload, req->arg_len, true);

		return stage_response(session, status, NULL, 0);
	}

	case CMD_CLEAR_FLAG: {
		s32 status = handle_set_flag(req->payload, req->arg_len, false);

		return stage_response(session, status, NULL, 0);
	}

	default:
		return stage_response(session, CMD_STATUS_UNKNOWN, NULL, 0);
	}
}

/* ------------------------------------------------------------------ *
 *  File operations
 * ------------------------------------------------------------------ */

/*
 * open - allocate per-open session state for /dev/companion.
 */
static int companion_open(struct inode *inode, struct file *file)
{
	struct companion_session *session;

	pr_info("companion: open\n");

	session = kzalloc(sizeof(*session), GFP_KERNEL);
	if (!session)
		return -ENOMEM;

	mutex_init(&session->lock);
	file->private_data = session;

	return 0;
}

/*
 * release - free per-open session state.
 */
static int companion_release(struct inode *inode, struct file *file)
{
	struct companion_session *session = file->private_data;

	pr_info("companion: release\n");

	if (session) {
		kfree(session->resp);
		mutex_destroy(&session->lock);
		kfree(session);
		file->private_data = NULL;
	}

	return 0;
}

/*
 * read - deliver the response staged by the last write().
 *
 * Returns the number of bytes copied, 0 when no response is pending (or it
 * has been fully consumed), or a negative errno on error. Supports partial
 * reads: a large response may be collected over several calls.
 */
static ssize_t companion_read(struct file *file, char __user *buf,
			      size_t len, loff_t *off)
{
	struct companion_session *session = file->private_data;
	size_t remaining;
	size_t to_copy;
	int ret;

	pr_info("companion: read (len=%zu)\n", len);

	if (mutex_lock_interruptible(&session->lock))
		return -ERESTARTSYS;

	if (!session->resp || session->resp_read >= session->resp_len) {
		to_copy = 0;
		goto out;
	}

	remaining = session->resp_len - session->resp_read;
	to_copy = min(len, remaining);

	if (copy_to_user(buf, session->resp + session->resp_read, to_copy)) {
		ret = -EFAULT;
		goto out_err;
	}

	session->resp_read += to_copy;

out:
	mutex_unlock(&session->lock);
	return (ssize_t)to_copy;

out_err:
	mutex_unlock(&session->lock);
	return ret;
}

/*
 * write - receive and dispatch one command from the companion.
 *
 * Copies the request into a bounded kernel buffer, validates its framing,
 * then dispatches it. The response is staged for the next read(). Rejects
 * malformed or oversized requests without touching any pending response.
 */
static ssize_t companion_write(struct file *file, const char __user *buf,
			       size_t len, loff_t *off)
{
	struct companion_session *session = file->private_data;
	struct cmd_request *req;
	size_t expected_payload;
	ssize_t ret;

	pr_info("companion: write (len=%zu)\n", len);

	/* A request must at least hold the fixed header. */
	if (len < sizeof(*req))
		return -EINVAL;

	/* Never allocate more than the agreed maximum from a user length. */
	if (len > COMPANION_MAX_REQUEST)
		return -EMSGSIZE;

	req = kmalloc(len, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	if (copy_from_user(req, buf, len)) {
		ret = -EFAULT;
		goto out;
	}

	/*
	 * arg_len is attacker-controlled: it must describe exactly the bytes
	 * that follow the header, no more and no less. Any mismatch is a
	 * framing error.
	 */
	expected_payload = len - sizeof(*req);
	if (req->arg_len != expected_payload) {
		ret = -EINVAL;
		goto out;
	}

	if (mutex_lock_interruptible(&session->lock)) {
		ret = -ERESTARTSYS;
		goto out;
	}

	if (dispatch_command(session, req))
		ret = -ENOMEM;
	else
		ret = (ssize_t)len;

	mutex_unlock(&session->lock);

out:
	kfree(req);
	return ret;
}

/*
 * unlocked_ioctl - alternative control path for the companion.
 *
 * Not used by the current protocol: the command channel goes through
 * write()/read(). Left as a clean no-op so callers fall back gracefully.
 */
static long companion_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	pr_info("companion: ioctl (cmd=0x%x)\n", cmd);
	return -ENOTTY;
}

/* ------------------------------------------------------------------ *
 *  Device descriptors
 * ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ *
 *  Public API (called by module_core)
 * ------------------------------------------------------------------ */

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
