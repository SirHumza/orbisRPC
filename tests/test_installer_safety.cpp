#include "settings.h"
#include "../installer/jsonlite.h"

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
	(void)rmdir(kDir);
	return 0;
}
