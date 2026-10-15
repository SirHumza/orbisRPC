/* installer_flow.h - the install wizard, callable from the merged app.
 *
 * The installer started as a standalone wizard with its own main(). The merged
 * app owns process lifetime and the menu, so this is now just the flow:
 * the update check, the already-installed check, the payload and config copies.
 * It drives the system dialogs directly.
 */
#ifndef INSTALLER_FLOW_H
#define INSTALLER_FLOW_H

/* Defined in C, called from the C++ app shell. */
#ifdef __cplusplus
extern "C" {
#endif

/* Step 2 of the wizard: ask GitHub for the newest release. When `localVersion`
 * is older than what GitHub has, prompts and, on yes, downloads the payload and
 * config into /data/temp/.
 *
 * `stagingPath` is accepted and ignored -- staging is fixed at /data/temp/.
 *
 * Returns 1 if both files were downloaded into staging, 0 otherwise. Never
 * fails: being offline means no update, not an aborted install. */
int installer_check_update(const char *localVersion, const char *stagingPath);

/* Runs the rest of the wizard: payload copy, config, option carry-over.
 *
 * Both sources come from the caller and always match -- either the staged
 * download or the package copies, never a mix.
 *
 * `replacing` means "there was a working install on this console and this run
 * took its place". It is not the same as "we downloaded something": on a console
 * with nothing installed and a newer release available, the download is correct
 * but nothing was replaced, and saying "Update done" there -- with a restart and
 * a toast -- describes something the user never had. installer_flow() decides
 * this before the update check runs, because the update path skips the
 * already-installed step and so has nowhere else to find out.
 *
 * `replacing` selects the wording, and gates the toast: the toast fires only for
 * a genuine update. Restart is separate and also covers a GoldHEN auto-run entry
 * being added, since that only takes effect on the next boot.
 *
 * `installedVersion` is what actually landed on disk, which on the update path
 * is the release's number rather than this binary's. It goes in the toast.
 * Leaves dialogs loaded on return; the merged app needs the IME. */
int installer_run(const char *payloadSource, const char *configSource,
                  int replacing, const char *installedVersion);

/* installer_flow() outcomes.
 *
 * Distinct codes rather than 0/1 because "the user already has it installed" is
 * not a failure, and reporting it as one puts the wrong word on the menu. */
enum installer_result
{
	INSTALLER_OK = 0,               /* payload and config are in place */
	INSTALLER_STOPPED = 1,          /* a step failed; see install.log */
	INSTALLER_ALREADY_INSTALLED = 2,/* nothing touched; uninstall first */
	INSTALLER_CANCELLED = 3         /* Circle; what was written was taken back */
};

/* Runs the full install/update wizard. Returns an enum installer_result.
 * Leaves dialogs shut down on return. */
int installer_flow(void);

#ifdef __cplusplus
}
#endif

#endif
