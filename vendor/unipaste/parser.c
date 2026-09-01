/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "unipaste.h"
#include "plugin.h"

/* Safe string duplicate */
static char *
xstrdup(const char *s)
{
	char *res;
	size_t len;

	if (!s)
		return NULL;
	len = strlen(s);
	res = malloc(len + 1);
	if (!res) {
		fprintf(stderr, "unipaste: memory allocation failed\n");
		exit(1);
	}
	memcpy(res, s, len + 1);
	return res;
}

/* Case-insensitive bounded prefix comparison helper */
static int
ci_n_equal(const char *a, const char *b, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		if (!a[i] || !b[i])
			return (a[i] == b[i]);
		if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
			return 0;
	}
	return 1;
}

/* Check if tag name matches */
static int
tag_is(const char *tag, const char *name)
{
	size_t len;
	const char *p;

	if (!tag || !name)
		return 0;
	while (isspace((unsigned char)*tag))
		tag++;
	if (*tag == '/')
		tag++;
	p = tag;
	while (*p && !isspace((unsigned char)*p) && *p != '/' && *p != '>')
		p++;
	len = (size_t)(p - tag);
	if (len != strlen(name))
		return 0;
	return ci_n_equal(tag, name, len);
}

/* Extract attribute value from tag string (e.g. href="...", class='...') */
static int
extract_attribute(const char *tag_str, const char *attr_name, char *val_out, size_t max_len)
{
	const char *p = tag_str;
	size_t attr_len;
	char quote;
	size_t i;

	if (!tag_str || !attr_name || !val_out || max_len == 0)
		return 0;

	attr_len = strlen(attr_name);
	val_out[0] = '\0';

	while (*p) {
		if (isspace((unsigned char)*p)) {
			p++;
			while (isspace((unsigned char)*p))
				p++;

			if (ci_n_equal(p, attr_name, attr_len) && (p[attr_len] == '=' || isspace((unsigned char)p[attr_len]))) {
				p += attr_len;
				while (isspace((unsigned char)*p))
					p++;
				if (*p == '=') {
					p++;
					while (isspace((unsigned char)*p))
						p++;
					if (*p == '"' || *p == '\'') {
						quote = *p++;
						i = 0;
						while (*p && *p != quote && i < max_len - 1) {
							val_out[i++] = *p++;
						}
						val_out[i] = '\0';
						decode_html_entities_inplace(val_out);
						return 1;
					} else {
						/* Unquoted attribute */
						i = 0;
						while (*p && !isspace((unsigned char)*p) && *p != '>' && i < max_len - 1) {
							val_out[i++] = *p++;
						}
						val_out[i] = '\0';
						decode_html_entities_inplace(val_out);
						return 1;
					}
				}
			}
		} else {
			p++;
		}
	}
	return 0;
}

/* Check if query parameter key is a known telemetry / tracking key */
static int
is_tracking_param(const char *key, size_t key_len)
{
	static const char *const exact_params[] = {
		"fbclid", "gclid", "gbraid", "wbraid", "dclid", "msclkid",
		"twclid", "igshid", "mc_cid", "mc_eid", "_hsenc", "_hsmi",
		"_openstat", "vero_id", "vero_conv", "yclid", "si", "feature",
		"rcm", "trk", "trackingId", "ref", "mkt_tok"
	};
	size_t i;

	if (key_len >= 4 && strncmp(key, "utm_", 4) == 0)
		return 1;
	if (key_len >= 4 && strncmp(key, "ref_", 4) == 0)
		return 1;

	for (i = 0; i < sizeof(exact_params) / sizeof(exact_params[0]); i++) {
		if (strlen(exact_params[i]) == key_len &&
		    strncmp(key, exact_params[i], key_len) == 0)
			return 1;
	}

	return 0;
}

/* Strip tracking query parameters from URL in-place */
void
url_strip_tracking_inplace(char *url)
{
	char *qmark, *hash, *p, *param_start;
	char cleaned_query[MAX_ATTR_VAL];
	size_t cleaned_len = 0;
	int has_kept_param = 0;
	size_t hash_len = 0;
	char hash_buf[MAX_ATTR_VAL];

	if (!url || !*url)
		return;

	qmark = strchr(url, '?');
	if (!qmark)
		return;

	hash = strchr(qmark, '#');
	if (hash) {
		hash_len = strlen(hash);
		if (hash_len < sizeof(hash_buf)) {
			memcpy(hash_buf, hash, hash_len + 1);
		} else {
			hash_buf[0] = '\0';
			hash_len = 0;
		}
		*hash = '\0';
	}

	p = qmark + 1;
	while (*p) {
		param_start = p;
		while (*p && *p != '&' && *p != ';')
			p++;

		char delim = *p;
		size_t param_len = (size_t)(p - param_start);

		if (param_len > 0) {
			const char *eq = memchr(param_start, '=', param_len);
			size_t key_len = eq ? (size_t)(eq - param_start) : param_len;

			if (!is_tracking_param(param_start, key_len)) {
				if (has_kept_param) {
					if (cleaned_len + 1 < sizeof(cleaned_query))
						cleaned_query[cleaned_len++] = '&';
				}
				if (cleaned_len + param_len < sizeof(cleaned_query)) {
					memcpy(cleaned_query + cleaned_len, param_start, param_len);
					cleaned_len += param_len;
				}
				has_kept_param = 1;
			}
		}

		if (delim == '&' || delim == ';')
			p++;
	}

	cleaned_query[cleaned_len] = '\0';

	if (has_kept_param && cleaned_len > 0) {
		*qmark = '?';
		memcpy(qmark + 1, cleaned_query, cleaned_len);
		qmark += 1 + cleaned_len;
	}

	if (hash_len > 0) {
		memcpy(qmark, hash_buf, hash_len);
		qmark += hash_len;
	}
	*qmark = '\0';
}

/* Emit newlines normalizing vertical whitespace */
static void
emit_newlines(struct parser_state *st, int count)
{
	int needed, i;

	if (st->table_depth > 0)
		return;

	needed = count - st->consecutive_newlines;
	if (needed <= 0)
		return;

	for (i = 0; i < needed; i++) {
		strbuf_putc(&st->outbuf, '\n');
		st->consecutive_newlines++;
	}
	st->at_line_start = 1;
	st->need_space = 0;
}

/* Emit indentation for blockquotes and lists */
static void
emit_indent(struct parser_state *st)
{
	int i;

	if (st->table_depth > 0 || !st->at_line_start)
		return;

	/* Blockquotes */
	for (i = 0; i < st->quote_depth; i++)
		strbuf_puts(&st->outbuf, "> ");

	/* Lists */
	if (st->list_depth > 0) {
		for (i = 0; i < (st->list_depth - 1) * 2; i++)
			strbuf_putc(&st->outbuf, ' ');
	}

	st->at_line_start = 0;
	st->need_space = 0;
}

/* Emit inline text to output or active buffer */
static void
emit_text(struct parser_state *st, const char *txt)
{
	if (!txt || !*txt)
		return;

	if (st->in_annotation) {
		strbuf_puts(&st->math_text, txt);
		return;
	}

	if (st->in_math) {
		/* Inside math container: ignore non-annotation visual text */
		return;
	}

	if (st->in_link) {
		if (st->need_space && st->link_text.len > 0) {
			strbuf_putc(&st->link_text, ' ');
		}
		st->need_space = 0;
		strbuf_puts(&st->link_text, txt);
		return;
	}

	if (st->table_depth > 0) {
		if (st->need_space && st->textbuf.len > 0) {
			strbuf_putc(&st->textbuf, ' ');
		}
		st->need_space = 0;
		strbuf_puts(&st->textbuf, txt);
		return;
	}

	if (st->heading_level > 0) {
		if (st->need_space && st->heading_text.len > 0) {
			strbuf_putc(&st->heading_text, ' ');
		}
		st->need_space = 0;
		strbuf_puts(&st->heading_text, txt);
		return;
	}

	if (st->at_line_start)
		emit_indent(st);

	if (st->need_space && !st->at_line_start && !st->pre_depth) {
		strbuf_putc(&st->outbuf, ' ');
		st->need_space = 0;
	}

	strbuf_puts(&st->outbuf, txt);
	st->consecutive_newlines = 0;
	st->at_line_start = 0;
}

/* Flush heading when closing h1-h6 */
static void
flush_heading(struct parser_state *st)
{
	int i, len;

	if (st->heading_level == 0)
		return;

	/* Headings inside table cells already landed in textbuf via emit_text. */
	if (st->table_depth > 0) {
		st->heading_level = 0;
		strbuf_reset(&st->heading_text);
		return;
	}

	emit_newlines(st, 2);
	emit_indent(st);

	switch (st->cfg->mode) {
	case MODE_MARKDOWN:
		for (i = 0; i < st->heading_level; i++)
			strbuf_putc(&st->outbuf, '#');
		strbuf_putc(&st->outbuf, ' ');
		strbuf_puts(&st->outbuf, st->heading_text.data);
		break;
	case MODE_SLACK:
		strbuf_putc(&st->outbuf, '*');
		strbuf_puts(&st->outbuf, st->heading_text.data);
		strbuf_putc(&st->outbuf, '*');
		break;
	case MODE_JIRA: {
		char hbuf[16];
		snprintf(hbuf, sizeof(hbuf), "h%d. ", st->heading_level);
		strbuf_puts(&st->outbuf, hbuf);
		strbuf_puts(&st->outbuf, st->heading_text.data);
		break;
	}
	case MODE_TERMINAL:
		strbuf_puts(&st->outbuf, "\033[1;36m"); /* Bold Cyan */
		for (i = 0; i < st->heading_level; i++)
			strbuf_putc(&st->outbuf, '#');
		strbuf_putc(&st->outbuf, ' ');
		strbuf_puts(&st->outbuf, st->heading_text.data);
		strbuf_puts(&st->outbuf, "\033[0m");
		break;
	case MODE_PLAIN:
	default:
		if (st->heading_level == 1) {
			strbuf_puts(&st->outbuf, st->heading_text.data);
			strbuf_putc(&st->outbuf, '\n');
			len = (int)st->heading_text.len;
			for (i = 0; i < len; i++)
				strbuf_putc(&st->outbuf, '=');
		} else if (st->heading_level == 2) {
			strbuf_puts(&st->outbuf, st->heading_text.data);
			strbuf_putc(&st->outbuf, '\n');
			len = (int)st->heading_text.len;
			for (i = 0; i < len; i++)
				strbuf_putc(&st->outbuf, '-');
		} else {
			for (i = 0; i < st->heading_level; i++)
				strbuf_putc(&st->outbuf, '#');
			strbuf_putc(&st->outbuf, ' ');
			strbuf_puts(&st->outbuf, st->heading_text.data);
		}
		break;
	}

	st->consecutive_newlines = 0;
	emit_newlines(st, 2);
	st->heading_level = 0;
	strbuf_reset(&st->heading_text);
}

/* Flush link when closing <a> */
static void
flush_link(struct parser_state *st)
{
	char *txt;
	char fn_buf[16];
	enum link_style lstyle;
	struct strbuf *target;

	if (!st->in_link)
		return;

	txt = st->link_text.data;
	lstyle = st->cfg->link_style;
	target = (st->table_depth > 0) ? &st->textbuf : &st->outbuf;

	if (lstyle == LINK_STYLE_AUTO) {
		if (st->cfg->mode == MODE_MARKDOWN)
			lstyle = LINK_STYLE_INLINE;
		else
			lstyle = LINK_STYLE_BRACKET;
	}

	if (st->table_depth == 0) {
		if (st->at_line_start)
			emit_indent(st);

		if (st->need_space && !st->at_line_start) {
			strbuf_putc(&st->outbuf, ' ');
			st->need_space = 0;
		}
	} else {
		if (st->need_space && st->textbuf.len > 0) {
			strbuf_putc(&st->textbuf, ' ');
			st->need_space = 0;
		}
	}

	if (st->current_href[0] == '\0' || lstyle == LINK_STYLE_TEXTONLY) {
		strbuf_puts(target, txt);
	} else if (st->cfg->mode == MODE_SLACK) {
		/* Slack mrkdwn link: <url|text> or <url> */
		if (strcmp(txt, st->current_href) == 0) {
			strbuf_puts(target, "<");
			strbuf_puts(target, st->current_href);
			strbuf_puts(target, ">");
		} else {
			strbuf_puts(target, "<");
			strbuf_puts(target, st->current_href);
			strbuf_puts(target, "|");
			strbuf_puts(target, txt);
			strbuf_puts(target, ">");
		}
	} else if (st->cfg->mode == MODE_JIRA) {
		/* Jira wiki link: [text|url] or [url] */
		if (strcmp(txt, st->current_href) == 0) {
			strbuf_puts(target, "[");
			strbuf_puts(target, st->current_href);
			strbuf_puts(target, "]");
		} else {
			strbuf_puts(target, "[");
			strbuf_puts(target, txt);
			strbuf_puts(target, "|");
			strbuf_puts(target, st->current_href);
			strbuf_puts(target, "]");
		}
	} else if (lstyle == LINK_STYLE_INLINE) {
		/* [text](url) */
		if (strcmp(txt, st->current_href) == 0) {
			strbuf_puts(target, "<");
			strbuf_puts(target, st->current_href);
			strbuf_puts(target, ">");
		} else {
			strbuf_puts(target, "[");
			strbuf_puts(target, txt);
			strbuf_puts(target, "](");
			strbuf_puts(target, st->current_href);
			strbuf_puts(target, ")");
		}
	} else if (lstyle == LINK_STYLE_FOOTNOTE) {
		if (st->num_footnotes < 255) {
			st->footnotes[st->num_footnotes] = xstrdup(st->current_href);
			st->num_footnotes++;
			strbuf_puts(target, txt);
			snprintf(fn_buf, sizeof(fn_buf), " [%d]", st->num_footnotes);
			strbuf_puts(target, fn_buf);
		} else {
			strbuf_puts(target, txt);
		}
	} else {
		/* LINK_STYLE_BRACKET: text (url) */
		if (strcmp(txt, st->current_href) == 0) {
			strbuf_puts(target, st->current_href);
		} else {
			strbuf_puts(target, txt);
			strbuf_puts(target, " (");
			strbuf_puts(target, st->current_href);
			strbuf_puts(target, ")");
		}
	}

	st->consecutive_newlines = 0;
	st->in_link = 0;
	st->current_href[0] = '\0';
	strbuf_reset(&st->link_text);
}

/* Extract language from pre or code tag attributes */
static const char *
extract_code_language(const char *tag_str, char *buf, size_t buf_size)
{
	char attr[MAX_ATTR_VAL];
	char *p;
	size_t i;

	if (extract_attribute(tag_str, "data-lang", attr, sizeof(attr)) ||
	    extract_attribute(tag_str, "data-code-language", attr, sizeof(attr)) ||
	    extract_attribute(tag_str, "lang", attr, sizeof(attr)) ||
	    extract_attribute(tag_str, "class", attr, sizeof(attr))) {

		p = strstr(attr, "language-");
		if (p) {
			p += 9;
		} else if ((p = strstr(attr, "lang-"))) {
			p += 5;
		} else if ((p = strstr(attr, "highlight-source-"))) {
			p += 17;
		} else if ((p = strstr(attr, "brush:"))) {
			p += 6;
			while (isspace((unsigned char)*p))
				p++;
		} else {
			p = attr;
		}

		i = 0;
		while (*p && !isspace((unsigned char)*p) && *p != ';' && i < buf_size - 1) {
			buf[i++] = *p++;
		}
		buf[i] = '\0';
		if (buf[0] != '\0')
			return buf;
	}
	return NULL;
}

/* Handle opening HTML tag */
static void
handle_open_tag(struct parser_state *st, const char *tag_str)
{
	char name[MAX_TAG_NAME];
	const char *p = tag_str;
	size_t i = 0;
	char attr_val[MAX_ATTR_VAL];

	while (*p && !isspace((unsigned char)*p) && *p != '>' && *p != '/' && i < MAX_TAG_NAME - 1)
		name[i++] = *p++;
	name[i] = '\0';

	/* Block level structural tags */
	if (tag_is(name, "p") || tag_is(name, "div") || tag_is(name, "article") ||
	    tag_is(name, "section") || tag_is(name, "main") || tag_is(name, "header") ||
	    tag_is(name, "footer")) {
		emit_newlines(st, 2);
	} else if (tag_is(name, "br")) {
		if (st->table_depth > 0) {
			strbuf_puts(&st->textbuf, " ");
		} else {
			strbuf_putc(&st->outbuf, '\n');
			st->consecutive_newlines = 1;
			st->at_line_start = 1;
			st->need_space = 0;
		}
	} else if (tag_is(name, "hr")) {
		emit_newlines(st, 2);
		emit_indent(st);
		if (st->cfg->mode == MODE_MARKDOWN)
			strbuf_puts(&st->outbuf, "---\n");
		else
			strbuf_puts(&st->outbuf, "----------------------------------------\n");
		st->consecutive_newlines = 1;
		st->at_line_start = 1;
	} else if (tag_is(name, "h1") || tag_is(name, "h2") || tag_is(name, "h3") ||
	           tag_is(name, "h4") || tag_is(name, "h5") || tag_is(name, "h6")) {
		st->heading_level = name[1] - '0';
		strbuf_reset(&st->heading_text);
		st->need_space = 0;
	} else if (tag_is(name, "ul")) {
		if (st->list_depth < MAX_LIST_DEPTH) {
			st->list_type[st->list_depth] = 1; /* UL */
			st->list_count[st->list_depth] = 0;
			st->list_depth++;
		}
		emit_newlines(st, 1);
	} else if (tag_is(name, "ol")) {
		if (st->list_depth < MAX_LIST_DEPTH) {
			st->list_type[st->list_depth] = 2; /* OL */
			st->list_count[st->list_depth] = 1;
			st->list_depth++;
		}
		emit_newlines(st, 1);
	} else if (tag_is(name, "li")) {
		char numbuf[32];
		emit_newlines(st, 1);
		emit_indent(st);
		if (st->list_depth > 0 && st->list_type[st->list_depth - 1] == 2) {
			/* Ordered list */
			snprintf(numbuf, sizeof(numbuf), "%d. ", st->list_count[st->list_depth - 1]++);
			strbuf_puts(&st->outbuf, numbuf);
		} else {
			/* Unordered list */
			strbuf_puts(&st->outbuf, "* ");
		}
		st->consecutive_newlines = 0;
		st->at_line_start = 0;
		st->need_space = 0;
	} else if (tag_is(name, "blockquote")) {
		st->quote_depth++;
		emit_newlines(st, 2);
	} else if (tag_is(name, "pre")) {
		st->pre_depth++;
		emit_newlines(st, 2);
		if (st->cfg->mode == MODE_MARKDOWN || st->cfg->mode == MODE_SLACK || st->cfg->mode == MODE_JIRA) {
			const char *lang = extract_code_language(tag_str, attr_val, sizeof(attr_val));
			if (lang && *lang) {
				if (st->cfg->mode == MODE_JIRA) {
					strbuf_puts(&st->outbuf, "{code:");
					strbuf_puts(&st->outbuf, lang);
					strbuf_puts(&st->outbuf, "}\n");
				} else if (st->cfg->mode == MODE_SLACK) {
					strbuf_puts(&st->outbuf, "```\n");
				} else {
					strbuf_puts(&st->outbuf, "```");
					strbuf_puts(&st->outbuf, lang);
					strbuf_putc(&st->outbuf, '\n');
				}
			} else {
				if (st->cfg->mode == MODE_JIRA)
					strbuf_puts(&st->outbuf, "{code}\n");
				else
					strbuf_puts(&st->outbuf, "```\n");
			}
			st->consecutive_newlines = 1;
			st->at_line_start = 1;
		}
	} else if (tag_is(name, "code")) {
		st->code_depth++;
		if (st->pre_depth > 0 && (st->cfg->mode == MODE_MARKDOWN || st->cfg->mode == MODE_JIRA)) {
			const char *lang = extract_code_language(tag_str, attr_val, sizeof(attr_val));
			if (lang && *lang) {
				if (st->outbuf.len >= 4 && strcmp(st->outbuf.data + st->outbuf.len - 4, "```\n") == 0) {
					st->outbuf.len -= 4;
					st->outbuf.data[st->outbuf.len] = '\0';
					strbuf_puts(&st->outbuf, "```");
					strbuf_puts(&st->outbuf, lang);
					strbuf_putc(&st->outbuf, '\n');
				} else if (st->outbuf.len >= 7 && strcmp(st->outbuf.data + st->outbuf.len - 7, "{code}\n") == 0) {
					st->outbuf.len -= 7;
					st->outbuf.data[st->outbuf.len] = '\0';
					strbuf_puts(&st->outbuf, "{code:");
					strbuf_puts(&st->outbuf, lang);
					strbuf_puts(&st->outbuf, "}\n");
				}
			}
		} else if (!st->pre_depth) {
			if (st->cfg->mode == MODE_MARKDOWN || st->cfg->mode == MODE_SLACK)
				emit_text(st, "`");
			else if (st->cfg->mode == MODE_JIRA)
				emit_text(st, "{{");
		}
	} else if (tag_is(name, "math")) {
		if (st->in_math == 0)
			strbuf_reset(&st->math_text);
		st->in_math++;
		if (extract_attribute(tag_str, "display", attr_val, sizeof(attr_val)) &&
		    strcmp(attr_val, "block") == 0) {
			st->math_display = 1;
		}
	} else if (tag_is(name, "annotation")) {
		if (extract_attribute(tag_str, "encoding", attr_val, sizeof(attr_val))) {
			if (strstr(attr_val, "tex") || strstr(attr_val, "latex") || strstr(attr_val, "LaTeX") || strstr(attr_val, "TeX")) {
				st->in_annotation = 1;
				strbuf_reset(&st->math_text);
			}
		}
	} else if (tag_is(name, "span")) {
		if (extract_attribute(tag_str, "class", attr_val, sizeof(attr_val))) {
			if (strstr(attr_val, "katex-display") || strstr(attr_val, "MathJax_Display")) {
				if (st->in_math == 0)
					strbuf_reset(&st->math_text);
				st->in_math++;
				st->math_display = 1;
			} else if (strstr(attr_val, "katex") || strstr(attr_val, "MathJax") || strstr(attr_val, "mwe-math")) {
				if (st->in_math == 0)
					strbuf_reset(&st->math_text);
				st->in_math++;
			}
		}
	} else if (tag_is(name, "img")) {
		char alt_val[MAX_ATTR_VAL];
		char src_val[MAX_ATTR_VAL];
		int has_alt = extract_attribute(tag_str, "alt", alt_val, sizeof(alt_val));
		int has_src = extract_attribute(tag_str, "src", src_val, sizeof(src_val));
		if (has_alt && alt_val[0]) {
			if (st->cfg->mode == MODE_MARKDOWN) {
				if (has_src && src_val[0]) {
					emit_text(st, "![");
					emit_text(st, alt_val);
					emit_text(st, "](");
					emit_text(st, src_val);
					emit_text(st, ")");
				} else {
					emit_text(st, "[Image: ");
					emit_text(st, alt_val);
					emit_text(st, "]");
				}
			} else {
				emit_text(st, "[Image: ");
				emit_text(st, alt_val);
				emit_text(st, "]");
			}
		}
	} else if (tag_is(name, "u") || tag_is(name, "ins")) {
		st->underline_depth++;
		if (st->cfg->mode == MODE_TERMINAL)
			emit_text(st, "\033[4m");
		else if (st->cfg->mode == MODE_JIRA)
			emit_text(st, "+");
	} else if (tag_is(name, "a")) {
		if (st->need_space && !st->at_line_start && !st->table_depth) {
			strbuf_putc(&st->outbuf, ' ');
			st->need_space = 0;
		}
		if (extract_attribute(tag_str, "href", st->current_href, sizeof(st->current_href))) {
			if (!st->cfg->keep_tracking)
				url_strip_tracking_inplace(st->current_href);
			st->in_link = 1;
			strbuf_reset(&st->link_text);
		}
	} else if (tag_is(name, "table")) {
		if (st->table_depth < MAX_TABLE_DEPTH) {
			if (st->table_depth > 0) {
				/* Preserve parent cell text across inner <td> resets. */
				strbuf_reset(&st->cell_save[st->table_depth]);
				if (st->textbuf.len > 0)
					strbuf_append(&st->cell_save[st->table_depth],
					              st->textbuf.data, st->textbuf.len);
				strbuf_reset(&st->textbuf);
			}
			st->table_stack[st->table_depth++] = table_create();
			if (st->table_depth == 1)
				emit_newlines(st, 2);
		}
	} else if (tag_is(name, "tr")) {
		if (st->table_depth > 0)
			table_add_row(st->table_stack[st->table_depth - 1]);
	} else if (tag_is(name, "th") || tag_is(name, "td")) {
		strbuf_reset(&st->textbuf);
		st->need_space = 0;
		st->pending_colspan = 1;
		if (extract_attribute(tag_str, "colspan", attr_val, sizeof(attr_val))) {
			int cs = atoi(attr_val);
			if (cs > 0)
				st->pending_colspan = cs;
		}
	} else if (tag_is(name, "b") || tag_is(name, "strong")) {
		st->bold_depth++;
		if (st->cfg->mode == MODE_MARKDOWN)
			emit_text(st, "**");
		else if (st->cfg->mode == MODE_SLACK && !st->table_depth)
			emit_text(st, "*");
		else if (st->cfg->mode == MODE_JIRA)
			emit_text(st, "*");
		else if (st->cfg->mode == MODE_TERMINAL)
			emit_text(st, "\033[1m");
	} else if (tag_is(name, "i") || tag_is(name, "em")) {
		st->italic_depth++;
		if (st->cfg->mode == MODE_MARKDOWN)
			emit_text(st, "*");
		else if (st->cfg->mode == MODE_SLACK && !st->table_depth)
			emit_text(st, "_");
		else if (st->cfg->mode == MODE_JIRA)
			emit_text(st, "_");
		else if (st->cfg->mode == MODE_TERMINAL)
			emit_text(st, "\033[3m");
	} else if (tag_is(name, "s") || tag_is(name, "del") || tag_is(name, "strike")) {
		st->strike_depth++;
		if (st->cfg->mode == MODE_MARKDOWN)
			emit_text(st, "~~");
		else if (st->cfg->mode == MODE_SLACK && !st->table_depth)
			emit_text(st, "~");
		else if (st->cfg->mode == MODE_JIRA)
			emit_text(st, "-");
	} else if (tag_is(name, "input")) {
		if (strstr(tag_str, "type=\"checkbox\"") || strstr(tag_str, "type='checkbox'")) {
			if (strstr(tag_str, "checked"))
				emit_text(st, "[x]");
			else
				emit_text(st, "[ ]");
			st->need_space = 1;
		}
	}
}

/* Handle closing HTML tag */
static void
handle_close_tag(struct parser_state *st, const char *tag_str)
{
	char name[MAX_TAG_NAME];
	const char *p = tag_str;
	size_t i = 0;

	while (*p && !isspace((unsigned char)*p) && *p != '>' && i < MAX_TAG_NAME - 1)
		name[i++] = *p++;
	name[i] = '\0';

	if (tag_is(name, "p") || tag_is(name, "div") || tag_is(name, "article") ||
	    tag_is(name, "section") || tag_is(name, "main") || tag_is(name, "header") ||
	    tag_is(name, "footer")) {
		emit_newlines(st, 2);
	} else if (tag_is(name, "h1") || tag_is(name, "h2") || tag_is(name, "h3") ||
	           tag_is(name, "h4") || tag_is(name, "h5") || tag_is(name, "h6")) {
		flush_heading(st);
	} else if (tag_is(name, "ul") || tag_is(name, "ol")) {
		if (st->list_depth > 0)
			st->list_depth--;
		emit_newlines(st, 1);
	} else if (tag_is(name, "li")) {
		emit_newlines(st, 1);
	} else if (tag_is(name, "blockquote")) {
		if (st->quote_depth > 0)
			st->quote_depth--;
		emit_newlines(st, 2);
	} else if (tag_is(name, "pre")) {
		if (st->pre_depth > 0) {
			st->pre_depth--;
			if (st->cfg->mode == MODE_MARKDOWN || st->cfg->mode == MODE_SLACK) {
				emit_newlines(st, 1);
				strbuf_puts(&st->outbuf, "```\n");
				st->consecutive_newlines = 1;
				st->at_line_start = 1;
			} else if (st->cfg->mode == MODE_JIRA) {
				emit_newlines(st, 1);
				strbuf_puts(&st->outbuf, "{code}\n");
				st->consecutive_newlines = 1;
				st->at_line_start = 1;
			}
			emit_newlines(st, 2);
		}
	} else if (tag_is(name, "code")) {
		if (st->code_depth > 0) {
			st->code_depth--;
			if (!st->pre_depth) {
				if (st->cfg->mode == MODE_MARKDOWN || st->cfg->mode == MODE_SLACK)
					emit_text(st, "`");
				else if (st->cfg->mode == MODE_JIRA)
					emit_text(st, "}}");
			}
		}
	} else if (tag_is(name, "u") || tag_is(name, "ins")) {
		if (st->underline_depth > 0) {
			st->underline_depth--;
			if (st->cfg->mode == MODE_TERMINAL)
				emit_text(st, "\033[0m");
			else if (st->cfg->mode == MODE_JIRA)
				emit_text(st, "+");
		}
	} else if (tag_is(name, "a")) {
		flush_link(st);
	} else if (tag_is(name, "table")) {
		if (st->table_depth > 1) {
			struct strbuf inner_buf;
			strbuf_init(&inner_buf, 512);
			table_render_inline(st->table_stack[st->table_depth - 1], &inner_buf);
			table_free(st->table_stack[st->table_depth - 1]);
			st->table_stack[st->table_depth - 1] = NULL;
			st->table_depth--;

			strbuf_reset(&st->textbuf);
			if (st->cell_save[st->table_depth].len > 0) {
				strbuf_append(&st->textbuf,
				              st->cell_save[st->table_depth].data,
				              st->cell_save[st->table_depth].len);
				strbuf_reset(&st->cell_save[st->table_depth]);
				if (st->textbuf.len > 0 &&
				    st->textbuf.data[st->textbuf.len - 1] != ' ')
					strbuf_putc(&st->textbuf, ' ');
			}
			if (inner_buf.len > 0)
				strbuf_append(&st->textbuf, inner_buf.data, inner_buf.len);
			strbuf_free(&inner_buf);
		} else if (st->table_depth == 1) {
			if (st->textbuf.len > 0) {
				table_add_cell(st->table_stack[0], st->textbuf.data, 0);
				strbuf_reset(&st->textbuf);
			}
			table_render(st->table_stack[0], &st->outbuf, st->cfg);
			table_free(st->table_stack[0]);
			st->table_stack[0] = NULL;
			st->table_depth = 0;
			st->consecutive_newlines = 1;
			emit_newlines(st, 2);
		}
	} else if (tag_is(name, "th") || tag_is(name, "td")) {
		if (st->table_depth > 0)
			table_add_cell_span(st->table_stack[st->table_depth - 1],
			                   st->textbuf.data, tag_is(name, "th"),
			                   st->pending_colspan);
		strbuf_reset(&st->textbuf);
		st->need_space = 0;
		st->pending_colspan = 1;
	} else if (tag_is(name, "b") || tag_is(name, "strong")) {
		if (st->bold_depth > 0) {
			st->bold_depth--;
			if (st->cfg->mode == MODE_MARKDOWN)
				emit_text(st, "**");
			else if (st->cfg->mode == MODE_SLACK && !st->table_depth)
				emit_text(st, "*");
			else if (st->cfg->mode == MODE_JIRA)
				emit_text(st, "*");
			else if (st->cfg->mode == MODE_TERMINAL)
				emit_text(st, "\033[0m");
		}
	} else if (tag_is(name, "i") || tag_is(name, "em")) {
		if (st->italic_depth > 0) {
			st->italic_depth--;
			if (st->cfg->mode == MODE_MARKDOWN)
				emit_text(st, "*");
			else if (st->cfg->mode == MODE_SLACK && !st->table_depth)
				emit_text(st, "_");
			else if (st->cfg->mode == MODE_JIRA)
				emit_text(st, "_");
			else if (st->cfg->mode == MODE_TERMINAL)
				emit_text(st, "\033[0m");
		}
	} else if (tag_is(name, "s") || tag_is(name, "del") || tag_is(name, "strike")) {
		if (st->strike_depth > 0) {
			st->strike_depth--;
			if (st->cfg->mode == MODE_MARKDOWN)
				emit_text(st, "~~");
			else if (st->cfg->mode == MODE_SLACK && !st->table_depth)
				emit_text(st, "~");
			else if (st->cfg->mode == MODE_JIRA)
				emit_text(st, "-");
		}
	} else if (tag_is(name, "annotation")) {
		st->in_annotation = 0;
	} else if (tag_is(name, "math") || (tag_is(name, "span") && st->in_math > 0)) {
		if (st->in_math > 0) {
			st->in_math--;
			if (st->in_math == 0 && st->math_text.len > 0) {
				if (st->math_display) {
					emit_newlines(st, 2);
					emit_indent(st);
					strbuf_puts(&st->outbuf, "$$\n");
					strbuf_puts(&st->outbuf, st->math_text.data);
					strbuf_puts(&st->outbuf, "\n$$");
					emit_newlines(st, 2);
				} else {
					if (st->need_space && !st->at_line_start)
						strbuf_putc(&st->outbuf, ' ');
					strbuf_putc(&st->outbuf, '$');
					strbuf_puts(&st->outbuf, st->math_text.data);
					strbuf_putc(&st->outbuf, '$');
					st->need_space = 1;
				}
				strbuf_reset(&st->math_text);
				st->math_display = 0;
			}
		}
	}
}

/* Strip Windows CF_HTML clipboard headers (Version:..., StartHTML:..., StartFragment:...) */
static void
strip_windows_cf_html_header(const char **input_ptr, size_t *len_ptr)
{
	const char *input = *input_ptr;
	size_t len = *len_ptr;

	if (!input || len == 0)
		return;

	/* Check if input begins with Windows CF_HTML header */
	if (len >= 8 && (ci_n_equal(input, "Version:", 8) || ci_n_equal(input, "version:", 8))) {
		const char *sf = strstr(input, "StartFragment:");
		const char *ef = strstr(input, "EndFragment:");
		if (sf && ef) {
			long start_offset = atol(sf + 14);
			long end_offset = atol(ef + 12);
			if (start_offset > 0 && start_offset < (long)len && end_offset > start_offset && end_offset <= (long)len) {
				input = input + start_offset;
				len = (size_t)(end_offset - start_offset);
				*input_ptr = input;
				*len_ptr = len;
				return;
			}
		}

		/* If numeric offsets were omitted or invalid, skip past header lines to first '<' */
		const char *first_tag = strchr(input, '<');
		if (first_tag && first_tag < input + len) {
			len -= (size_t)(first_tag - input);
			input = first_tag;
			*input_ptr = input;
			*len_ptr = len;
		}
	}

	/* Also check for fragment comment markers <!--StartFragment--> ... <!--EndFragment--> */
	const char *frag_start = strstr(input, "<!--StartFragment-->");
	if (frag_start && frag_start < input + len) {
		frag_start += strlen("<!--StartFragment-->");
		const char *frag_end = strstr(frag_start, "<!--EndFragment-->");
		if (frag_end && frag_end <= input + len) {
			len = (size_t)(frag_end - frag_start);
		} else {
			len -= (size_t)(frag_start - input);
		}
		input = frag_start;
		*input_ptr = input;
		*len_ptr = len;
	}
}

/* Check if raw plain text input is a TSV (tab-separated) spreadsheet grid */
static int
is_tsv_grid(const char *s, size_t len)
{
	size_t tabs = 0;
	size_t lines = 0;
	size_t i;
	int has_html_tag = 0;
	size_t cur_row_tabs = 0;
	size_t first_row_tabs = 0;
	int rows_with_tabs = 0;

	for (i = 0; i < len; i++) {
		if (s[i] == '\t') {
			tabs++;
			cur_row_tabs++;
		} else if (s[i] == '\n') {
			lines++;
			if (cur_row_tabs > 0) {
				rows_with_tabs++;
				if (first_row_tabs == 0)
					first_row_tabs = cur_row_tabs;
			}
			cur_row_tabs = 0;
		} else if (s[i] == '<' && i + 1 < len && (isalpha((unsigned char)s[i + 1]) || s[i + 1] == '/' || s[i + 1] == '!')) {
			has_html_tag = 1;
		}
	}

	if (cur_row_tabs > 0) {
		rows_with_tabs++;
		if (first_row_tabs == 0)
			first_row_tabs = cur_row_tabs;
	}

	if (has_html_tag || tabs == 0)
		return 0;

	/* Multi-row spreadsheet copied from Excel/Sheets: at least 2 rows with tabs */
	if (rows_with_tabs >= 2)
		return 1;

	/* Single row: require at least 2 tabs (3 columns) and newline or clear length to avoid single indented line */
	if (rows_with_tabs == 1 && first_row_tabs >= 2 && lines > 0)
		return 1;

	return 0;
}

static int
process_tsv_to_strbuf(const char *input, size_t len, struct strbuf *out, const struct config *cfg)
{
	struct table *t = table_create();
	const char *p = input;
	const char *end = input + len;
	const char *cell_start;
	char cell_buf[4096];
	size_t cell_len;
	int is_first_row = 1;

	table_add_row(t);
	while (p < end && *p) {
		cell_start = p;
		while (p < end && *p != '\t' && *p != '\n' && *p != '\r')
			p++;

		cell_len = (size_t)(p - cell_start);
		if (cell_len >= sizeof(cell_buf))
			cell_len = sizeof(cell_buf) - 1;
		memcpy(cell_buf, cell_start, cell_len);
		cell_buf[cell_len] = '\0';

		table_add_cell(t, cell_buf, is_first_row);

		if (p < end && *p == '\t') {
			p++;
		} else if (p < end && (*p == '\n' || *p == '\r')) {
			if (*p == '\r' && p + 1 < end && p[1] == '\n')
				p++;
			p++;
			is_first_row = 0;
			if (p < end && *p)
				table_add_row(t);
		}
	}

	table_render(t, out, cfg);
	table_free(t);
	return 0;
}

/* Main parsing loop for HTML input buffer into strbuf */
int
unipaste_process_to_strbuf(const char *input, size_t len, struct strbuf *out, const struct config *cfg)
{
	struct parser_state st;
	const char *p, *end;
	char entity_buf[16];
	size_t consumed;
	int in_tag = 0;
	int in_script_or_style = 0;
	char char_buf[8];
	int i;
	char *sanitized = NULL;
	char *alloc_input = NULL;
	size_t j;

	if (!input || !out || !cfg)
		return 1;

	/* Allocate a null-terminated working buffer for safe parsing of raw binary/fuzzer inputs */
	alloc_input = malloc(len + 1);
	if (!alloc_input)
		return 1;
	memcpy(alloc_input, input, len);
	alloc_input[len] = '\0';
	input = alloc_input;

	/* Strip Windows CF_HTML clipboard headers if present */
	strip_windows_cf_html_header(&input, &len);

	/* Auto-detect raw TSV grids from Excel / Google Sheets */
	if (is_tsv_grid(input, len)) {
		int res = process_tsv_to_strbuf(input, len, out, cfg);
		free(alloc_input);
		return res;
	}

	/* Plugin hook: sanitize input before formatting if compiled in */
	sanitized = sanitize_html(input, len);
	if (sanitized) {
		free(alloc_input);
		alloc_input = sanitized;
		input = alloc_input;
		len = strlen(alloc_input);
	}

	memset(&st, 0, sizeof(st));
	st.cfg = cfg;
	st.consecutive_newlines = 2; /* Treat start as beginning of fresh paragraph */
	st.at_line_start = 1;

	strbuf_init(&st.outbuf, len ? len * 2 : 4096);
	strbuf_init(&st.linebuf, 1024);
	strbuf_init(&st.tagbuf, 256);
	strbuf_init(&st.textbuf, 1024);
	strbuf_init(&st.link_text, 256);
	strbuf_init(&st.heading_text, 256);
	strbuf_init(&st.math_text, 256);
	for (i = 0; i < MAX_TABLE_DEPTH; i++)
		strbuf_init(&st.cell_save[i], 128);

	p = input;
	end = p + len;

	while (p < end && *p) {
		/* HTML comment parsing */
		if (!in_tag && !in_script_or_style && *p == '<' && p + 3 < end && p[1] == '!' && p[2] == '-' && p[3] == '-') {
			p += 4;
			while (p + 2 < end && !(p[0] == '-' && p[1] == '-' && p[2] == '>'))
				p++;
			if (p + 2 < end)
				p += 3;
			else
				p = end;
			continue;
		}

		/* Tag Start */
		if (*p == '<') {
			in_tag = 1;
			strbuf_reset(&st.tagbuf);
			p++;
			while (p < end && *p != '>') {
				if (st.tagbuf.len < 65536)
					strbuf_putc(&st.tagbuf, *p);
				p++;
			}
			if (p < end && *p == '>')
				p++;

			in_tag = 0;

			/* Check script/style boundary */
			if (st.tagbuf.data[0] == '/') {
				if (tag_is(st.tagbuf.data + 1, "script") || tag_is(st.tagbuf.data + 1, "style") || tag_is(st.tagbuf.data + 1, "head")) {
					in_script_or_style = 0;
				} else if (!in_script_or_style) {
					handle_close_tag(&st, st.tagbuf.data + 1);
				}
			} else {
				if (tag_is(st.tagbuf.data, "script") || tag_is(st.tagbuf.data, "style") || tag_is(st.tagbuf.data, "head")) {
					in_script_or_style = 1;
				} else if (!in_script_or_style) {
					handle_open_tag(&st, st.tagbuf.data);
				}
			}
			continue;
		}

		if (in_script_or_style) {
			p++;
			continue;
		}

		/* Preformatted text and Math annotations: preserve whitespace and characters verbatim */
		if (st.pre_depth > 0 || st.in_annotation) {
			if (*p == '&') {
				consumed = decode_html_entity(p, entity_buf, sizeof(entity_buf));
				if (consumed > 0) {
					emit_text(&st, entity_buf);
					p += consumed;
					continue;
				}
			}
			if (*p == '\n') {
				if (st.in_annotation) {
					strbuf_putc(&st.math_text, '\n');
				} else {
					strbuf_putc(&st.outbuf, '\n');
					st.consecutive_newlines = 1;
					st.at_line_start = 1;
				}
			} else {
				char_buf[0] = *p;
				char_buf[1] = '\0';
				emit_text(&st, char_buf);
			}
			p++;
			continue;
		}

		/* Normal text whitespace handling: collapse multiple whitespace into single space */
		if (isspace((unsigned char)*p)) {
			st.need_space = 1;
			p++;
			while (p < end && isspace((unsigned char)*p))
				p++;
			continue;
		}

		/* HTML entity decoding */
		if (*p == '&') {
			consumed = decode_html_entity(p, entity_buf, sizeof(entity_buf));
			if (consumed > 0) {
				emit_text(&st, entity_buf);
				p += consumed;
				continue;
			}
		}

		/* Normal character */
		char_buf[0] = *p++;
		char_buf[1] = '\0';
		emit_text(&st, char_buf);
	}

	/* Emit any pending footnotes */
	if (st.num_footnotes > 0) {
		emit_newlines(&st, 2);
		for (i = 0; i < st.num_footnotes; i++) {
			char fn_header[32];
			snprintf(fn_header, sizeof(fn_header), "[%d] ", i + 1);
			strbuf_puts(&st.outbuf, fn_header);
			if (st.footnotes[i])
				strbuf_puts(&st.outbuf, st.footnotes[i]);
			strbuf_putc(&st.outbuf, '\n');
			free(st.footnotes[i]);
			st.footnotes[i] = NULL;
		}
	}

	/* Flush any unclosed tables */
	while (st.table_depth > 0) {
		if (st.textbuf.len > 0) {
			table_add_cell(st.table_stack[st.table_depth - 1], st.textbuf.data, 0);
			strbuf_reset(&st.textbuf);
		}
		table_render(st.table_stack[st.table_depth - 1], &st.outbuf, st.cfg);
		table_free(st.table_stack[st.table_depth - 1]);
		st.table_stack[st.table_depth - 1] = NULL;
		st.table_depth--;
	}

	/* Ensure trailing newline */
	if (st.outbuf.len > 0 && st.outbuf.data[st.outbuf.len - 1] != '\n')
		strbuf_putc(&st.outbuf, '\n');

	/* Output result with CRLF conversion if requested */
	if (cfg->crlf) {
		for (j = 0; j < st.outbuf.len; j++) {
			if (st.outbuf.data[j] == '\n' && (j == 0 || st.outbuf.data[j - 1] != '\r'))
				strbuf_putc(out, '\r');
			strbuf_putc(out, st.outbuf.data[j]);
		}
	} else {
		strbuf_append(out, st.outbuf.data, st.outbuf.len);
	}

	/* Clean up */
	strbuf_free(&st.outbuf);
	strbuf_free(&st.linebuf);
	strbuf_free(&st.tagbuf);
	strbuf_free(&st.textbuf);
	strbuf_free(&st.link_text);
	strbuf_free(&st.heading_text);
	strbuf_free(&st.math_text);
	for (i = 0; i < MAX_TABLE_DEPTH; i++)
		strbuf_free(&st.cell_save[i]);
	free(alloc_input);

	return 0;
}

/* Process input string and write to stream */
int
unipaste_process_string(const char *input, size_t len, FILE *out, const struct config *cfg)
{
	struct strbuf sb;
	int ret;

	if (!input || !out || !cfg)
		return 1;

	strbuf_init(&sb, len ? len * 2 : 4096);
	ret = unipaste_process_to_strbuf(input, len, &sb, cfg);
	if (ret == 0 && sb.len > 0) {
		fwrite(sb.data, 1, sb.len, out);
	}
	strbuf_free(&sb);
	return ret;
}

/* Process input from stream */
int
unipaste_process_stream(FILE *in, FILE *out, const struct config *cfg)
{
	struct strbuf input_buf;
	char chunk[4096];
	size_t n;
	int res;

	if (!in || !out || !cfg)
		return 1;

	strbuf_init(&input_buf, 8192);
	while ((n = fread(chunk, 1, sizeof(chunk), in)) > 0) {
		strbuf_append(&input_buf, chunk, n);
	}

	res = unipaste_process_string(input_buf.data, input_buf.len, out, cfg);
	strbuf_free(&input_buf);
	return res;
}
