/* jsonlite.c - minimal JSON reader/writer for the installer config. */

#include "jsonlite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JL_MAX_DEPTH 64

typedef struct
{
	const char *p;
	const char *end;
	int         depth;
} jl_parser;

static jl_val_t *parse_value(jl_parser *ps);

/* ---- construction --------------------------------------------------------- */

static jl_val_t *alloc_value(jl_type_t type)
{
	jl_val_t *v = (jl_val_t *)calloc(1, sizeof(jl_val_t));

	if (v)
		v->type = type;

	return v;
}

jl_val_t *jl_new_object(void) { return alloc_value(JL_OBJECT); }
jl_val_t *jl_new_array(void)  { return alloc_value(JL_ARRAY); }

jl_val_t *jl_new_string(const char *text)
{
	jl_val_t *v = alloc_value(JL_STRING);
	size_t len;

	if (!v)
		return NULL;

	if (!text)
		text = "";

	len = strlen(text);
	v->str = (char *)malloc(len + 1);
	if (!v->str)
	{
		free(v);
		return NULL;
	}

	memcpy(v->str, text, len + 1);
	return v;
}

jl_val_t *jl_new_int(long value)
{
	jl_val_t *v = alloc_value(JL_NUMBER);

	if (v)
	{
		v->num_is_int = 1;
		v->inum = value;
		v->num = (double)value;
	}

	return v;
}

jl_val_t *jl_new_double(double value)
{
	jl_val_t *v = alloc_value(JL_NUMBER);

	if (v)
	{
		v->num_is_int = 0;
		v->num = value;
	}

	return v;
}

jl_val_t *jl_new_bool(int value)
{
	jl_val_t *v = alloc_value(JL_BOOL);

	if (v)
		v->boolean = value ? 1 : 0;

	return v;
}

/* Frees everything a value owns -- children, string, key -- but not the value
 * itself. Used when a node's contents are replaced in place. */
static void free_contents(jl_val_t *v)
{
	jl_val_t *child = v->child;

	while (child)
	{
		jl_val_t *next = child->next;
		jl_free(child);
		child = next;
	}

	free(v->str);
	free(v->key);

	v->child = NULL;
	v->next = NULL;
	v->str = NULL;
	v->key = NULL;
}

void jl_free(jl_val_t *value)
{
	if (!value)
		return;

	free_contents(value);
	free(value);
}

/* ---- containers ------------------------------------------------------------ */

jl_val_t *jl_obj_get(const jl_val_t *object, const char *key)
{
	jl_val_t *p;

	if (!object || object->type != JL_OBJECT || !key)
		return NULL;

	for (p = object->child; p; p = p->next)
	{
		if (p->key && strcmp(p->key, key) == 0)
			return p;
	}

	return NULL;
}

void jl_obj_set(jl_val_t *object, const char *key, jl_val_t *value)
{
	jl_val_t *p, *last = NULL;

	if (!object || object->type != JL_OBJECT || !key || !value)
	{
		jl_free(value);
		return;
	}

	/* Replace in place so the key keeps its position in the output. The node
	 * itself must survive: it is still linked into the list and owns the key,
	 * so both its key and its `next` sibling link are left untouched. */
	for (p = object->child; p; p = p->next)
	{
		if (p->key && strcmp(p->key, key) == 0)
		{
			jl_val_t *oldChild = p->child;

			/* Release only the payload being replaced. */
			while (oldChild)
			{
				jl_val_t *next = oldChild->next;
				jl_free(oldChild);
				oldChild = next;
			}
			free(p->str);

			p->type = value->type;
			p->num_is_int = value->num_is_int;
			p->inum = value->inum;
			p->num = value->num;
			p->boolean = value->boolean;
			p->str = value->str;
			p->child = value->child;

			free(value->key);
			free(value);
			return;
		}
		last = p;
	}

	value->key = (char *)malloc(strlen(key) + 1);
	if (!value->key)
	{
		jl_free(value);
		return;
	}
	strcpy(value->key, key);

	if (last)
		last->next = value;
	else
		object->child = value;
}

/* ---- parsing --------------------------------------------------------------- */

static void skip_ws(jl_parser *ps)
{
	while (ps->p < ps->end)
	{
		const char c = *ps->p;

		if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			++ps->p;
		else
			break;
	}
}

/* Appends a code point as UTF-8. */
static void append_utf8(char **buf, size_t *len, size_t *cap, unsigned cp)
{
	char scratch[4];
	size_t n = 0;

	if (cp < 0x80)
	{
		scratch[n++] = (char)cp;
	}
	else if (cp < 0x800)
	{
		scratch[n++] = (char)(0xC0 | (cp >> 6));
		scratch[n++] = (char)(0x80 | (cp & 0x3F));
	}
	else if (cp < 0x10000)
	{
		scratch[n++] = (char)(0xE0 | (cp >> 12));
		scratch[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
		scratch[n++] = (char)(0x80 | (cp & 0x3F));
	}
	else
	{
		scratch[n++] = (char)(0xF0 | (cp >> 18));
		scratch[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
		scratch[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
		scratch[n++] = (char)(0x80 | (cp & 0x3F));
	}

	if (*len + n + 1 > *cap)
	{
		const size_t want = (*len + n + 1) * 2;
		char *grown = (char *)realloc(*buf, want);

		if (!grown)
			return;

		*buf = grown;
		*cap = want;
	}

	memcpy(*buf + *len, scratch, n);
	*len += n;
}

static int read_hex4(const char *p, unsigned *out)
{
	unsigned value = 0;
	int i;

	for (i = 0; i < 4; ++i)
	{
		const char c = p[i];
		value <<= 4;

		if (c >= '0' && c <= '9')      value |= (unsigned)(c - '0');
		else if (c >= 'a' && c <= 'f') value |= (unsigned)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') value |= (unsigned)(c - 'A' + 10);
		else return -1;
	}

	*out = value;
	return 0;
}

/* Reads a quoted string starting at the opening quote. Returns a malloc'd
 * C string, or NULL on malformed input. */
static char *parse_string_raw(jl_parser *ps)
{
	char *buf = NULL;
	size_t len = 0, cap = 0;

	if (ps->p >= ps->end || *ps->p != '"')
		return NULL;

	++ps->p;

	for (;;)
	{
		unsigned char c;

		if (ps->p >= ps->end)
		{
			free(buf);
			return NULL;
		}

		c = (unsigned char)*ps->p;

		if (c == '"')
		{
			++ps->p;
			break;
		}

		if (c == '\\')
		{
			++ps->p;

			if (ps->p >= ps->end)
			{
				free(buf);
				return NULL;
			}

			switch (*ps->p)
			{
			case '"':  append_utf8(&buf, &len, &cap, '"');  ++ps->p; break;
			case '\\': append_utf8(&buf, &len, &cap, '\\'); ++ps->p; break;
			case '/':  append_utf8(&buf, &len, &cap, '/');  ++ps->p; break;
			case 'b':  append_utf8(&buf, &len, &cap, '\b'); ++ps->p; break;
			case 'f':  append_utf8(&buf, &len, &cap, '\f'); ++ps->p; break;
			case 'n':  append_utf8(&buf, &len, &cap, '\n'); ++ps->p; break;
			case 'r':  append_utf8(&buf, &len, &cap, '\r'); ++ps->p; break;
			case 't':  append_utf8(&buf, &len, &cap, '\t'); ++ps->p; break;
			case 'u':
			{
				unsigned cp = 0;

				if (ps->p + 5 > ps->end ||
				    read_hex4(ps->p + 1, &cp) != 0)
				{
					free(buf);
					return NULL;
				}

				ps->p += 5;

				/* Recombine a surrogate pair when the low half follows. */
				if (cp >= 0xD800 && cp <= 0xDBFF &&
				    ps->p + 6 <= ps->end &&
				    ps->p[0] == '\\' && ps->p[1] == 'u')
				{
					unsigned low = 0;

					if (read_hex4(ps->p + 2, &low) == 0 &&
					    low >= 0xDC00 && low <= 0xDFFF)
					{
						cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
						ps->p += 6;
					}
				}

				append_utf8(&buf, &len, &cap, cp);
				break;
			}
			default:
				free(buf);
				return NULL;
			}

			continue;
		}

		if (c < 0x20)
		{
			/* Raw control characters are not legal inside a JSON string. */
			free(buf);
			return NULL;
		}

		append_utf8(&buf, &len, &cap, c);
		++ps->p;
	}

	if (!buf)
	{
		buf = (char *)malloc(1);
		if (!buf)
			return NULL;
		buf[0] = '\0';
		return buf;
	}

	buf[len] = '\0';
	return buf;
}

static jl_val_t *parse_number(jl_parser *ps)
{
	const char *start = ps->p;
	char scratch[64];
	size_t n;
	int is_int = 1;
	jl_val_t *v;

	if (ps->p < ps->end && (*ps->p == '-' || *ps->p == '+'))
		++ps->p;

	while (ps->p < ps->end)
	{
		const char c = *ps->p;

		if (c >= '0' && c <= '9') { ++ps->p; continue; }
		if (c == '.')             { is_int = 0; ++ps->p; continue; }
		if (c == 'e' || c == 'E') { is_int = 0; ++ps->p; continue; }
		if (c == '-' || c == '+')
		{
			/* Only valid straight after an exponent marker. */
			if (ps->p > start && (ps->p[-1] == 'e' || ps->p[-1] == 'E'))
			{
				++ps->p;
				continue;
			}
			break;
		}
		break;
	}

	n = (size_t)(ps->p - start);
	if (n == 0 || n >= sizeof scratch)
		return NULL;

	memcpy(scratch, start, n);
	scratch[n] = '\0';

	v = alloc_value(JL_NUMBER);
	if (!v)
		return NULL;

	if (is_int)
	{
		v->num_is_int = 1;
		v->inum = strtol(scratch, NULL, 10);
		v->num = (double)v->inum;
	}
	else
	{
		v->num_is_int = 0;
		v->num = strtod(scratch, NULL);
	}

	return v;
}

static int literal(jl_parser *ps, const char *word)
{
	const size_t n = strlen(word);

	if ((size_t)(ps->end - ps->p) < n || memcmp(ps->p, word, n) != 0)
		return -1;

	ps->p += n;
	return 0;
}

static jl_val_t *parse_object(jl_parser *ps)
{
	jl_val_t *object = alloc_value(JL_OBJECT);
	jl_val_t *last = NULL;

	if (!object)
		return NULL;

	++ps->p; /* '{' */
	skip_ws(ps);

	if (ps->p < ps->end && *ps->p == '}')
	{
		++ps->p;
		return object;
	}

	for (;;)
	{
		char *key;
		jl_val_t *value;

		skip_ws(ps);
		key = parse_string_raw(ps);
		if (!key)
			goto fail;

		skip_ws(ps);
		if (ps->p >= ps->end || *ps->p != ':')
		{
			free(key);
			goto fail;
		}
		++ps->p;

		value = parse_value(ps);
		if (!value)
		{
			free(key);
			goto fail;
		}

		value->key = key;

		if (last)
			last->next = value;
		else
			object->child = value;

		last = value;

		skip_ws(ps);
		if (ps->p < ps->end && *ps->p == ',')
		{
			++ps->p;
			continue;
		}

		if (ps->p < ps->end && *ps->p == '}')
		{
			++ps->p;
			return object;
		}

		goto fail;
	}

fail:
	jl_free(object);
	return NULL;
}

static jl_val_t *parse_array(jl_parser *ps)
{
	jl_val_t *array = alloc_value(JL_ARRAY);
	jl_val_t *last = NULL;

	if (!array)
		return NULL;

	++ps->p; /* '[' */
	skip_ws(ps);

	if (ps->p < ps->end && *ps->p == ']')
	{
		++ps->p;
		return array;
	}

	for (;;)
	{
		jl_val_t *value = parse_value(ps);

		if (!value)
			goto fail;

		if (last)
			last->next = value;
		else
			array->child = value;

		last = value;

		skip_ws(ps);
		if (ps->p < ps->end && *ps->p == ',')
		{
			++ps->p;
			continue;
		}

		if (ps->p < ps->end && *ps->p == ']')
		{
			++ps->p;
			return array;
		}

		goto fail;
	}

fail:
	jl_free(array);
	return NULL;
}

static jl_val_t *parse_value(jl_parser *ps)
{
	jl_val_t *v;

	if (ps->depth >= JL_MAX_DEPTH)
		return NULL;

	skip_ws(ps);
	if (ps->p >= ps->end)
		return NULL;

	switch (*ps->p)
	{
	case '{':
		++ps->depth;
		v = parse_object(ps);
		--ps->depth;
		return v;

	case '[':
		++ps->depth;
		v = parse_array(ps);
		--ps->depth;
		return v;

	case '"':
	{
		char *s = parse_string_raw(ps);

		if (!s)
			return NULL;

		v = alloc_value(JL_STRING);
		if (!v)
		{
			free(s);
			return NULL;
		}

		v->str = s;
		return v;
	}

	case 't':
		if (literal(ps, "true") != 0)
			return NULL;
		return jl_new_bool(1);

	case 'f':
		if (literal(ps, "false") != 0)
			return NULL;
		return jl_new_bool(0);

	case 'n':
		if (literal(ps, "null") != 0)
			return NULL;
		return alloc_value(JL_NULL);

	default:
		return parse_number(ps);
	}
}

jl_val_t *jl_parse(const char *text, size_t len)
{
	jl_parser ps;
	jl_val_t *root;

	if (!text)
		return NULL;

	ps.p = text;
	ps.end = text + len;
	ps.depth = 0;

	root = parse_value(&ps);
	if (!root)
		return NULL;

	skip_ws(&ps);
	if (ps.p != ps.end)
	{
		/* Trailing garbage means the document is not what it claims. */
		jl_free(root);
		return NULL;
	}

	return root;
}

/* ---- serialising ----------------------------------------------------------- */

typedef struct
{
	char  *buf;
	size_t len;
	size_t cap;
	int    failed;
} jl_writer;

static void writer_put(jl_writer *w, const char *text, size_t n)
{
	if (w->failed)
		return;

	if (w->len + n + 1 > w->cap)
	{
		const size_t want = (w->len + n + 1) * 2;
		char *grown = (char *)realloc(w->buf, want);

		if (!grown)
		{
			w->failed = 1;
			return;
		}

		w->buf = grown;
		w->cap = want;
	}

	memcpy(w->buf + w->len, text, n);
	w->len += n;
	w->buf[w->len] = '\0';
}

static void writer_puts(jl_writer *w, const char *text)
{
	writer_put(w, text, strlen(text));
}

static void write_string(jl_writer *w, const char *s)
{
	writer_put(w, "\"", 1);

	if (s)
	{
		const unsigned char *p = (const unsigned char *)s;

		for (; *p; ++p)
		{
			switch (*p)
			{
			case '"':  writer_put(w, "\\\"", 2); break;
			case '\\': writer_put(w, "\\\\", 2); break;
			case '\b': writer_put(w, "\\b", 2);  break;
			case '\f': writer_put(w, "\\f", 2);  break;
			case '\n': writer_put(w, "\\n", 2);  break;
			case '\r': writer_put(w, "\\r", 2);  break;
			case '\t': writer_put(w, "\\t", 2);  break;
			default:
				if (*p < 0x20)
				{
					char esc[7];
					snprintf(esc, sizeof esc, "\\u%04x", *p);
					writer_put(w, esc, 6);
				}
				else
				{
					writer_put(w, (const char *)p, 1);
				}
				break;
			}
		}
	}

	writer_put(w, "\"", 1);
}

static void write_value(jl_writer *w, const jl_val_t *v)
{
	char scratch[64];

	if (!v)
	{
		writer_puts(w, "null");
		return;
	}

	switch (v->type)
	{
	case JL_NULL:
		writer_puts(w, "null");
		break;

	case JL_BOOL:
		writer_puts(w, v->boolean ? "true" : "false");
		break;

	case JL_NUMBER:
		if (v->num_is_int)
			snprintf(scratch, sizeof scratch, "%ld", v->inum);
		else
			snprintf(scratch, sizeof scratch, "%.17g", v->num);
		writer_puts(w, scratch);
		break;

	case JL_STRING:
		write_string(w, v->str);
		break;

	case JL_ARRAY:
	{
		const jl_val_t *p;
		int first = 1;

		writer_put(w, "[", 1);

		for (p = v->child; p; p = p->next)
		{
			if (!first)
				writer_put(w, ",", 1);
			first = 0;
			write_value(w, p);
		}

		writer_put(w, "]", 1);
		break;
	}

	case JL_OBJECT:
	{
		const jl_val_t *p;
		int first = 1;

		writer_put(w, "{", 1);

		for (p = v->child; p; p = p->next)
		{
			if (!first)
				writer_put(w, ",", 1);
			first = 0;

			write_string(w, p->key ? p->key : "");
			writer_put(w, ":", 1);
			write_value(w, p);
		}

		writer_put(w, "}", 1);
		break;
	}

	default:
		writer_puts(w, "null");
		break;
	}
}

char *jl_stringify(const jl_val_t *value)
{
	jl_writer w;

	w.buf = NULL;
	w.len = 0;
	w.cap = 0;
	w.failed = 0;

	write_value(&w, value);

	if (w.failed)
	{
		free(w.buf);
		return NULL;
	}

	if (!w.buf)
	{
		w.buf = (char *)malloc(1);
		if (!w.buf)
			return NULL;
		w.buf[0] = '\0';
	}

	return w.buf;
}
