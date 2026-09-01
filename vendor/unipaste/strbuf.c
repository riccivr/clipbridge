/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "unipaste.h"

void
strbuf_init(struct strbuf *sb, size_t initial_cap)
{
	if (!sb)
		return;

	if (initial_cap < 32)
		initial_cap = 32;

	sb->data = malloc(initial_cap);
	if (!sb->data) {
		sb->len = 0;
		sb->cap = 0;
		return;
	}
	sb->data[0] = '\0';
	sb->len = 0;
	sb->cap = initial_cap;
}

void
strbuf_free(struct strbuf *sb)
{
	if (sb && sb->data) {
		free(sb->data);
		sb->data = NULL;
		sb->len = 0;
		sb->cap = 0;
	}
}

void
strbuf_reset(struct strbuf *sb)
{
	if (sb && sb->data) {
		sb->data[0] = '\0';
		sb->len = 0;
	}
}

static void
strbuf_grow(struct strbuf *sb, size_t needed)
{
	size_t min_cap;
	size_t new_cap;
	char *new_data;

	if (!sb || !sb->data)
		return;

	/* Prevent size_t integer overflow */
	if (needed > (SIZE_MAX - sb->len - 1)) {
		return;
	}

	min_cap = sb->len + needed + 1;
	if (min_cap <= sb->cap)
		return;

	if (sb->cap > SIZE_MAX / 2)
		new_cap = min_cap;
	else
		new_cap = sb->cap * 2;

	if (new_cap < min_cap)
		new_cap = min_cap;

	new_data = realloc(sb->data, new_cap);
	if (!new_data) {
		return;
	}
	sb->data = new_data;
	sb->cap = new_cap;
}

void
strbuf_putc(struct strbuf *sb, char c)
{
	if (!sb || !sb->data)
		return;
	strbuf_grow(sb, 1);
	if (sb->len + 1 < sb->cap) {
		sb->data[sb->len++] = c;
		sb->data[sb->len] = '\0';
	}
}

void
strbuf_puts(struct strbuf *sb, const char *s)
{
	size_t slen;

	if (!sb || !sb->data || !s || !*s)
		return;
	slen = strlen(s);
	strbuf_grow(sb, slen);
	if (sb->len + slen < sb->cap) {
		memcpy(sb->data + sb->len, s, slen);
		sb->len += slen;
		sb->data[sb->len] = '\0';
	}
}

void
strbuf_append(struct strbuf *sb, const char *data, size_t len)
{
	if (!sb || !sb->data || !data || len == 0)
		return;
	strbuf_grow(sb, len);
	if (sb->len + len < sb->cap) {
		memcpy(sb->data + sb->len, data, len);
		sb->len += len;
		sb->data[sb->len] = '\0';
	}
}
