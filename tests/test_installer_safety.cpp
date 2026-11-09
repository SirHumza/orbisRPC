#include "settings.h"
#include "../installer/jsonlite.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {

const char *const kConfig = "./installer-safety-fixture/config.json";
const char *const kDir = "./installer-safety-fixture";

void Write(const char *text)
{
	FILE *f = fopen(kConfig, "wb");
	assert(f);
	assert(fputs(text, f) >= 0);
	assert(fclose(f) == 0);
}

char *Read()
{
	FILE *f = fopen(kConfig, "rb");
	assert(f);
	assert(fseek(f, 0, SEEK_END) == 0);
	long n = ftell(f);
	assert(n >= 0);
	assert(fseek(f, 0, SEEK_SET) == 0);
	char *text = static_cast<char *>(malloc(static_cast<size_t>(n) + 1));
	assert(text);
	assert(fread(text, 1, static_cast<size_t>(n), f) == static_cast<size_t>(n));
	text[n] = 0;
	assert(fclose(f) == 0);
	return text;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc == 5 && strcmp(argv[1], "rotate") == 0)
	{
		const int readyFd = atoi(argv[2]);
		const int releaseFd = atoi(argv[3]);
		const int lockFd = open(argv[4], O_CREAT | O_RDWR, 0600);
		assert(lockFd >= 0);
		struct flock lock = {};
		lock.l_type = F_WRLCK;
		lock.l_whence = SEEK_SET;
		assert(fcntl(lockFd, F_SETLKW, &lock) == 0);
		char signal = 'x';
		assert(write(readyFd, &signal, 1) == 1);
		close(readyFd);
		assert(read(releaseFd, &signal, 1) == 1);
		close(releaseFd);
		Write("{\"token\":\"daemon.rotated.token\",\"show_firmware\":true}");
		lock.l_type = F_UNLCK;
		assert(fcntl(lockFd, F_SETLK, &lock) == 0);
		close(lockFd);
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "save") == 0)
	{
		const int readyFd = atoi(argv[2]);
		Settings childSettings;
		childSettings.Load();
		childSettings.Toggle(kSetShowFirmware);
		char signal = 'x';
		assert(write(readyFd, &signal, 1) == 1);
		close(readyFd);
		return childSettings.Save() == 0 ? 0 : 1;
	}

	char *saved;
	const char embedded_nul[] = "{\"token\":\"prefix.valid.token\\u0000suffix\"}";
	jl_val_t *with_nul = jl_parse(embedded_nul, sizeof embedded_nul - 1);
	assert(with_nul == nullptr);
	jl_free(with_nul);

	(void)mkdir(kDir, 0700);
	(void)remove(kConfig);
	const char duplicate_token[] =
		"{\"token\":\"first.token.value\",\"token\":\"second.token.value\","
		"\"qos_token\":\"old-a\",\"qos_token\":\"old-b\"}";
	Write(duplicate_token);
	Settings ambiguous;
	ambiguous.Load();
	ambiguous.Toggle(kSetShowFirmware);
	assert(ambiguous.Save() == -1);
	saved = Read();
	assert(strcmp(saved, duplicate_token) == 0);
	free(saved);
	ambiguous.SetToken("replacement.token.value-with-valid-shaped-segments");
	assert(ambiguous.Save() == 0);
	saved = Read();
	jl_val_t *repaired = jl_parse(saved, strlen(saved));
	int token_count = 0;
	for (jl_val_t *p = repaired ? repaired->child : nullptr; p; p = p->next)
		if (p->key && strcmp(p->key, "token") == 0)
			++token_count;
	assert(repaired && token_count == 1);
	assert(strcmp(jl_obj_get(repaired, "token")->str,
	              "replacement.token.value-with-valid-shaped-segments") == 0);
	assert(jl_obj_count(repaired, "qos_token") == 1);
	assert(strcmp(jl_obj_get(repaired, "qos_token")->str, "") == 0);
	jl_free(repaired);
	free(saved);
	(void)remove(kConfig);

	// Boolean settings are JSON booleans in both readers. Numeric values are
	// ignored by the UI, matching the daemon's typed boolean handling.
	Write("{\"show_firmware\":0,\"show_media\":2.5}");
	Settings numericToggles;
	numericToggles.Load();
	assert(numericToggles.Save() == 0);
	saved = Read();
	jl_val_t *normalizedToggles = jl_parse(saved, strlen(saved));
	assert(normalizedToggles);
	jl_val_t *firmwareToggle = jl_obj_get(normalizedToggles, "show_firmware");
	jl_val_t *mediaToggle = jl_obj_get(normalizedToggles, "show_media");
	assert(firmwareToggle && firmwareToggle->type == JL_BOOL && firmwareToggle->boolean == 1);
	assert(mediaToggle && mediaToggle->type == JL_BOOL && mediaToggle->boolean == 1);
	jl_free(normalizedToggles);
	free(saved);
	(void)remove(kConfig);

	// An unrelated settings save keeps nested/unknown fields and a token that
	// changed on disk after this Settings object was loaded.
	Write("{\"token\":\"old.token.value\",\"unknown\":{\"nested\":7},\"show_firmware\":true}");
	Settings settings;
	settings.Load();
	Write("{\"token\":\"new.token.value\",\"unknown\":{\"nested\":7},\"show_firmware\":true}");
	settings.Toggle(kSetShowFirmware);
	assert(settings.Save() == 0);
	saved = Read();
	assert(strstr(saved, "\"token\":\"new.token.value\""));
	assert(strstr(saved, "\"unknown\":{\"nested\":7}"));
	free(saved);

	// Installer and daemon writers serialize the complete read-modify-write.
	// Hold the shared sidecar lock while a child attempts Save, then change the
	// token as the daemon would before releasing it. The child must preserve it.
	Write("{\"token\":\"old.token.value\",\"show_firmware\":true}");
	char lockPath[512];
	const int lockPathLen = snprintf(lockPath, sizeof lockPath, "%s.lock", kConfig);
	assert(lockPathLen > 0 && static_cast<size_t>(lockPathLen) < sizeof lockPath);
	int ready[2], release[2];
	assert(pipe(ready) == 0 && pipe(release) == 0);
	pid_t locker = fork();
	assert(locker >= 0);
	if (locker == 0)
	{
		close(ready[0]);
		close(release[1]);
		char readyText[16], releaseText[16];
		assert(snprintf(readyText, sizeof readyText, "%d", ready[1]) > 0);
		assert(snprintf(releaseText, sizeof releaseText, "%d", release[0]) > 0);
		execl(argv[0], argv[0], "rotate", readyText, releaseText, lockPath,
		      static_cast<char *>(nullptr));
		_exit(127);
	}
	close(ready[1]);
	close(release[0]);
	char signal;
	assert(read(ready[0], &signal, 1) == 1 && signal == 'x');
	close(ready[0]);
	int childReady[2];
	assert(pipe(childReady) == 0);
	pid_t child = fork();
	assert(child >= 0);
	if (child == 0)
	{
		close(childReady[0]);
		char fdText[16];
		assert(snprintf(fdText, sizeof fdText, "%d", childReady[1]) > 0);
		execl(argv[0], argv[0], "save", fdText, static_cast<char *>(nullptr));
		_exit(127);
	}
	close(childReady[1]);
	assert(read(childReady[0], &signal, 1) == 1 && signal == 'x');
	close(childReady[0]);
	struct timespec pause = {0, 50000000};
	nanosleep(&pause, nullptr);
	int status = 0;
	const pid_t early = waitpid(child, &status, WNOHANG);
	if (early != 0)
		fprintf(stderr, "save exited while lock held: pid=%d status=%d\n", (int)early, status);
	assert(early == 0);
	assert(write(release[1], "x", 1) == 1);
	close(release[1]);
	assert(waitpid(locker, &status, 0) == locker);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	saved = Read();
	assert(strstr(saved, "\"token\":\"daemon.rotated.token\""));
	free(saved);

	Write("{\"token\":\"old.token.value\",\"qos_token\":\"signed-old\","
	      "\"unknown\":7}");
	Settings tokenEdit;
	tokenEdit.Load();
	tokenEdit.SetToken("replacement.token.value-with-valid-shaped-segments");
	assert(tokenEdit.Save() == 0);
	saved = Read();
	jl_val_t *edited = jl_parse(saved, strlen(saved));
	assert(edited);
	assert(strcmp(jl_obj_get(edited, "token")->str,
	              "replacement.token.value-with-valid-shaped-segments") == 0);
	assert(strcmp(jl_obj_get(edited, "qos_token")->str, "") == 0);
	assert(jl_obj_get(edited, "unknown")->inum == 7);
	jl_free(edited);
	free(saved);

	// A malformed existing file is left byte-for-byte intact when Save fails.
	Write("{this is not valid config");
	Settings malformed;
	malformed.Load();
	assert(malformed.Save() == -1);
	saved = Read();
	assert(strcmp(saved, "{this is not valid config") == 0);
	free(saved);

	(void)remove(kConfig);
	(void)remove("./installer-safety-fixture/config.json.lock");
	(void)rmdir(kDir);
	return 0;
}
