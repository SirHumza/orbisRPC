#include "settings.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

int main()
{
	(void)mkdir(kDir, 0700);
	(void)remove(kConfig);

	// An unrelated settings save keeps nested/unknown fields and a token that
	// changed on disk after this Settings object was loaded.
	Write("{\"token\":\"old.token.value\",\"unknown\":{\"nested\":7},\"show_firmware\":true}");
	Settings settings;
	settings.Load();
	Write("{\"token\":\"new.token.value\",\"unknown\":{\"nested\":7},\"show_firmware\":true}");
	settings.Toggle(kSetShowFirmware);
	assert(settings.Save() == 0);
	char *saved = Read();
	assert(strstr(saved, "\"token\":\"new.token.value\""));
	assert(strstr(saved, "\"unknown\":{\"nested\":7}"));
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
	(void)rmdir(kDir);
	return 0;
}
