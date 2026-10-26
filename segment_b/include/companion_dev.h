/* SPDX-License-Identifier: GPL-2.0 */
/*
 * companion_dev.h - Public API of the command channel (segment B).
 *
 * Declares the entry points that module_core.c uses to bring the companion
 * misc device up and down. Including this header in companion_dev.c as well
 * gives every non-static function a visible prototype, as required by the
 * kernel build (-Wmissing-prototypes).
 */
#ifndef ROOTKIT_B_COMPANION_DEV_H
#define ROOTKIT_B_COMPANION_DEV_H

/* Create /dev/companion. Returns 0 on success or a negative errno. */
int companion_dev_register(void);

/* Remove /dev/companion. */
void companion_dev_unregister(void);

#endif /* ROOTKIT_B_COMPANION_DEV_H */
