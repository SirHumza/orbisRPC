/* notify.h - on-screen notifications from the payload.
 *
 * sceKernelSendNotificationRequest takes a PATH for the icon, not a memory
 * blob, so the icon is dropped on disk once at first boot from an embedded
 * copy (icon_embedded.h) and referenced by path afterwards.
 *
 * Every call is best-effort: a notification that fails must never affect the
 * daemon, so nothing here returns an error the caller has to handle.
 */
#ifndef ORBISRPC_NOTIFY_H
#define ORBISRPC_NOTIFY_H

/* Show a message with the orbisRPC icon. Never fails loudly. */
void notify_show(const char *message);

/* Once-only variants: safe to call every retry loop without spamming. */
void notify_once_boot(const char *key, const char *message);

/* Rate-limited variant for a condition that persists (Discord unreachable).
 * Fires on the first call, then at most once per `interval_s`. */
void notify_throttled(const char *key, const char *message, int interval_s);

#endif /* ORBISRPC_NOTIFY_H */