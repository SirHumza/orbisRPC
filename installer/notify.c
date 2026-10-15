/* notify.c - post-update system notification.
 *
 * Mechanism: sceKernelSendNotificationRequest, which the kernel routes to
 * SceShellUI by writing the request to /dev/notification0. This is the same
 * function the OS uses for its own toasts, which is why the struct layout is
 * documented publicly rather than in a header.
 *
 * The struct and offsets come from OSM-Made/PS4-Notify (MIT), reached via the
 * prototype in Scene-Collective/ps4-payload-sdk's libPS4/include/sysutil.h.
 * Not from this toolchain: <orbis/SysUtil.h> declares its notification NIDs as
 * void(void) with the parameters stripped, and libkernel.h does not declare
 * this one at all. So it is declared here.
 *
 * The layout is packed, and the size is checked at compile time below. A
 * mismatched size is a build failure rather than a malformed toast at runtime.
 */

#include "notify.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* ---- the request struct ---------------------------------------------
 * Field names and offsets as published. Message/IconImageUri are a union with
 * the arg1/arg2/arg3 form; the buffer member exists only to pin the size, and
 * the static check below fails the build if the packing is ever wrong. */
#define NOTIFY_TYPE_MESSAGE 0

#define NOTIFY_API_TOAST   0   /* SceShellUI toast; the other value writes to
                                * the notification database instead */

#pragma pack(push, 1)
typedef struct {
    int    Type;            /* 0x00 */
    uint32_t ReqId;         /* 0x04 */
    uint32_t Priority;      /* 0x08 */
    uint32_t MsgId;         /* 0x0C */
    int    TargetId;        /* 0x10 -- -1 broadcasts */
    uint32_t UserId;        /* 0x14 */
    uint32_t DeviceId;      /* 0x18 */
    uint32_t AddressingUserId; /* 0x1C */
    uint32_t AppId;         /* 0x20 */
    uint32_t ErrorNumber;   /* 0x24 */
    uint32_t Attribute;     /* 0x28 */
    uint8_t HasIcon;        /* 0x2C */
    union {
        struct {
            char Message[0x400];       /* 0x2D */
            char IconImageUri[0x800];  /* 0x42D */
        };
        struct {
            char arg1[180];
            char arg2[180];
            char arg3[180];
        };
        struct {
            char buffer[0xC03];        /* 0x2D */
        };
    };
} SceNotificationRequest;
#pragma pack(pop)

/* The kernel writes exactly this many bytes, so a wrong size means garbage on
 * the other end. Fail the build instead. */
typedef char notify_size_must_be_0xC30
    [sizeof(SceNotificationRequest) == 0xC30 ? 1 : -1];

/* Declared here because libkernel.h does not have it. Non-blocking: the fourth
 * argument is a bool, passed as int through the default-argument promotion. */
extern int sceKernelSendNotificationRequest(int api, char *buffer,
                                            size_t size, int blocking);

/* System icon, used when the user's own PNG is not on the console. */
#define SYSTEM_ICON "cxml://psnotification/tex_icon_system"

/* The URI handed to ShellUI.
 *
 * A fixed string rather than built from ICON_PATH, because the two are
 * deliberately different spellings of the same file: ICON_PATH is how this process
 * asks for it, ICON_URI is how the consumer names it. Deriving one from the other
 * is what produced the three-slash URI that never rendered.
 */

static void nlog(const char *tag, int err)
{
    FILE *f = fopen("/data/orbisRPC/install.log", "a");
    if (!f)
    {
        f = fopen("/data/install.log", "a");
        if (!f)
            return;
    }
    fprintf(f, "notify %s err=%d(%s)\n", tag, err, err ? strerror(err) : "ok");
    fclose(f);
}

static int readable(const char *path)
{
    FILE *f;
    int saved;

    errno = 0;
    f = fopen(path, "rb");
    if (f)
    {
        fclose(f);
        return 1;
    }

    /* Keep the reason. "No icon" has at least three causes -- the file is
     * missing, this process may not read it, or it is there and unreadable for
     * another reason -- and they need different fixes. The FTP daemon on the same
     * console could read this path while the app could not, which is exactly the
     * case that a plain 0/1 check cannot distinguish. */
    saved = errno;
    {
        char line[320];
        snprintf(line, sizeof line, "icon-open-failed path=%s errno=%d(%s)",
                 path, saved, saved ? strerror(saved) : "none");
        nlog(line, saved);
    }

    return 0;
}

int notify_update_installed(const char *version)
{
    /* Static, not stack: 0xC30 is 3120 bytes and this runs where the stack has
     * no room to spare. Zeroed, so the union and the unused header fields are
     * defined rather than whatever was there before. */
    static SceNotificationRequest req;

    const char *icon;
    int rc;

    if (!version || !version[0])
        version = "(unknown)";

    memset(&req, 0, sizeof req);

    req.Type = NOTIFY_TYPE_MESSAGE;
    req.Priority = 0;
    req.TargetId = -1;
    req.Attribute = 0;
    req.HasIcon = 1;

    snprintf(req.Message, sizeof req.Message,
             "orbisRPC updated to %s. Restart your console.", version);

    /* An absent icon is normal, not a failure: the toast still has to say what
     * happened. Falls back to the system icon rather than going out blank. */
    {
        const int have = readable(ICON_PATH);
        icon = have ? ICON_URI : SYSTEM_ICON;

        /* Which one was chosen, and the two spellings side by side, since the
         * whole point is that they differ. */
        {
            char line[320];
            snprintf(line, sizeof line, "icon=%s (checked %s, present: %s)",
                     icon, ICON_PATH, have ? "yes" : "no");
            nlog(line, 0);
        }
    }

    /* IconImageUri is 0x800 and strcpy would overrun a longer path silently.
     * Checked so a deep install path shows up in the log instead of arriving with
     * a cut-off URI. */
    if (strlen(icon) >= sizeof req.IconImageUri)
    {
        nlog("icon-too-long", 0);
        icon = SYSTEM_ICON;
    }
    strcpy(req.IconImageUri, icon);

    rc = sceKernelSendNotificationRequest(NOTIFY_API_TOAST, (char *)&req,
                                          sizeof req, 0);

    if (rc < 0)
    {
        nlog("failed", rc);
        return 0;
    }

    nlog("sent", 0);
    return 1;
}
