/* See LICENSE file for copyright and license details. */
/* Built-in HTML sanitizer: strips scripts, styles, iframes, and unsafe handlers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "unipaste.h"
#include "plugin.h"

/* List of allowed HTML tags */
static const char *allowed_tags[] = {
	"p", "br", "hr", "div", "span",
	"h1", "h2", "h3", "h4", "h5", "h6",
	"ul", "ol", "li",
	"table", "thead", "tbody", "tfoot", "tr", "th", "td",
	"pre", "code",
	"blockquote",
	"a", "b", "strong", "i", "em", "s", "del", "strike", "u",
	"input", "img",
	"math", "semantics", "annotation", "mrow", "mi", "mo", "mn", "msup", "msub", "mfrac", "msqrt", "mroot",
	NULL
};

/* List of dangerous tags whose inner content must also be discarded */
static const char *strip_content_tags[] = {
	"script", "style", "head", "iframe", "object", "embed",
	"applet", "svg", "canvas", "template", "noscript",
	NULL
};

/* List of allowed attribute names */
static const char *allowed_attrs[] = {
	"href", "class", "data-lang", "type", "checked",
	"colspan", "rowspan", "alt", "src", "title",
	"encoding", "display", "xmlns",
	NULL
};

static int
ci_str_equal(const char *a, const char *b)
{
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return 0;
		a++;
		b++;
	}
	return (*a == '\0' && *b == '\0');
}

static int
is_in_list(const char *name, const char *list[])
{
	size_t i;
	if (!name || !*name)
		return 0;
	for (i = 0; list[i]; i++) {
		if (ci_str_equal(name, list[i]))
			return 1;
	}
	return 0;
}

/* Check if a URI scheme is safe (http, https, mailto, ftp, or relative path) */
static int
is_safe_uri(const char *uri)
{
	const char *p = uri;

	if (!uri)
		return 0;

	/* Skip leading whitespace */
	while (isspace((unsigned char)*p))
		p++;

	/* Relative paths and anchors are safe */
	if (*p == '/' || *p == '.' || *p == '#' || *p == '?' || *p == '\0')
		return 1;

	/* Check for safe protocols */
	if (strncasecmp(p, "http://", 7) == 0 ||
	    strncasecmp(p, "https://", 8) == 0 ||
	    strncasecmp(p, "mailto:", 7) == 0 ||
	    strncasecmp(p, "ftp://", 6) == 0)
		return 1;

	/* Disallow javascript:, data:, vbscript:, and unknown protocols */
	return 0;
}

/* Extract tag name from '<tag ...>' or '</tag>' */
static void
get_tag_name(const char *tag, char *dest, size_t max_len)
{
	size_t i = 0;
	const char *p = tag;

	if (*p == '/')
		p++;

	while (*p && !isspace((unsigned char)*p) && *p != '>' && *p != '/' && i < max_len - 1)
		dest[i++] = *p++;
	dest[i] = '\0';
}

/* Sanitize attributes of an allowed tag and append to output strbuf */
static void
sanitize_and_append_tag(struct strbuf *sb, const char *raw_tag, size_t tag_len)
{
	char tag_name[MAX_TAG_NAME];
	char attr_name[MAX_TAG_NAME];
	char attr_val[MAX_ATTR_VAL];
	const char *p = raw_tag;
	const char *end = raw_tag + tag_len;
	int is_closing = 0;
	size_t i;
	char quote;

	if (tag_len == 0)
		return;

	if (*p == '/') {
		is_closing = 1;
		p++;
	}

	get_tag_name(is_closing ? raw_tag + 1 : raw_tag, tag_name, sizeof(tag_name));

	if (!is_in_list(tag_name, allowed_tags))
		return;

	strbuf_putc(sb, '<');
	if (is_closing) {
		strbuf_putc(sb, '/');
		strbuf_puts(sb, tag_name);
		strbuf_putc(sb, '>');
		return;
	}

	strbuf_puts(sb, tag_name);

	/* Advance p past the tag name */
	while (p < end && !isspace((unsigned char)*p) && *p != '>')
		p++;

	/* Parse and filter attributes */
	while (p < end && *p != '>') {
		while (p < end && isspace((unsigned char)*p))
			p++;

		if (p >= end || *p == '>' || *p == '/')
			break;

		/* Read attribute name */
		i = 0;
		while (p < end && !isspace((unsigned char)*p) && *p != '=' && *p != '>' && *p != '/' && i < sizeof(attr_name) - 1)
			attr_name[i++] = *p++;
		attr_name[i] = '\0';

		while (p < end && isspace((unsigned char)*p))
			p++;

		attr_val[0] = '\0';
		if (p < end && *p == '=') {
			p++;
			while (p < end && isspace((unsigned char)*p))
				p++;

			if (p < end && (*p == '"' || *p == '\'')) {
				quote = *p++;
				i = 0;
				while (p < end && *p != quote && i < sizeof(attr_val) - 1)
					attr_val[i++] = *p++;
				attr_val[i] = '\0';
				if (p < end && *p == quote)
					p++;
			} else {
				i = 0;
				while (p < end && !isspace((unsigned char)*p) && *p != '>' && i < sizeof(attr_val) - 1)
					attr_val[i++] = *p++;
				attr_val[i] = '\0';
			}
		}

		/* Validate attribute against allowlist */
		if (is_in_list(attr_name, allowed_attrs)) {
			/* Check URI safety for href and src */
			if (ci_str_equal(attr_name, "href") || ci_str_equal(attr_name, "src")) {
				if (!is_safe_uri(attr_val))
					continue;
			}

			strbuf_putc(sb, ' ');
			strbuf_puts(sb, attr_name);
			strbuf_puts(sb, "=\"");
			for (i = 0; attr_val[i]; i++) {
				if (attr_val[i] == '"')
					strbuf_puts(sb, "&quot;");
				else if (attr_val[i] == '<')
					strbuf_puts(sb, "&lt;");
				else if (attr_val[i] == '>')
					strbuf_puts(sb, "&gt;");
				else
					strbuf_putc(sb, attr_val[i]);
			}
			strbuf_putc(sb, '"');
		}
	}

	strbuf_putc(sb, '>');
}

/* Sanitize HTML string */
char *
sanitize_html(const char *input, size_t len)
{
	struct strbuf sb;
	const char *p, *end;
	char tag_name[MAX_TAG_NAME];
	int in_discard_block = 0;
	char discard_tag[MAX_TAG_NAME] = {0};
	struct strbuf cur_tag;

	if (!input)
		return NULL;

	strbuf_init(&sb, len ? len + 128 : 1024);
	strbuf_init(&cur_tag, 256);

	p = input;
	end = input + len;

	while (p < end && *p) {
		/* Comments */
		if (*p == '<' && p + 3 < end && p[1] == '!' && p[2] == '-' && p[3] == '-') {
			p += 4;
			while (p + 2 < end && !(p[0] == '-' && p[1] == '-' && p[2] == '>'))
				p++;
			if (p + 2 < end)
				p += 3;
			else
				p = end;
			continue;
		}

		/* Tags */
		if (*p == '<') {
			p++;
			strbuf_reset(&cur_tag);
			while (p < end && *p != '>') {
				if (cur_tag.len < 65536)
					strbuf_putc(&cur_tag, *p);
				p++;
			}
			if (p < end && *p == '>')
				p++;

			/* Check if closing tag */
			if (cur_tag.data[0] == '/') {
				get_tag_name(cur_tag.data + 1, tag_name, sizeof(tag_name));
				if (in_discard_block && ci_str_equal(tag_name, discard_tag)) {
					in_discard_block = 0;
					discard_tag[0] = '\0';
					continue;
				}
				if (in_discard_block)
					continue;
			} else {
				get_tag_name(cur_tag.data, tag_name, sizeof(tag_name));

				/* Check if entering a block whose contents should be stripped */
				if (is_in_list(tag_name, strip_content_tags)) {
					in_discard_block = 1;
					snprintf(discard_tag, sizeof(discard_tag), "%s", tag_name);
					continue;
				}
			}

			if (in_discard_block)
				continue;

			sanitize_and_append_tag(&sb, cur_tag.data, cur_tag.len);
			continue;
		}

		if (in_discard_block) {
			p++;
			continue;
		}

		/* Normal text character */
		strbuf_putc(&sb, *p++);
	}

	strbuf_free(&cur_tag);

	/* Return dynamically allocated null-terminated string */
	return sb.data;
}
