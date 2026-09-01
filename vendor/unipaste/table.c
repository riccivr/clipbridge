/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "unipaste.h"

/* Helper to duplicate string safely */
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

/* Decode one UTF-8 codepoint and advance pointer */
static unsigned long
utf8_next_codepoint(const char **src)
{
	const unsigned char *p = (const unsigned char *)*src;
	unsigned long cp = 0;
	int len = 0, i;

	if (!*p)
		return 0;

	if (*p < 0x80) {
		cp = *p;
		len = 1;
	} else if ((*p & 0xE0) == 0xC0) {
		cp = *p & 0x1F;
		len = 2;
	} else if ((*p & 0xF0) == 0xE0) {
		cp = *p & 0x0F;
		len = 3;
	} else if ((*p & 0xF8) == 0xF0) {
		cp = *p & 0x07;
		len = 4;
	} else {
		*src += 1;
		return 0xFFFD;
	}

	p++;
	for (i = 1; i < len; i++) {
		if ((*p & 0xC0) != 0x80) {
			*src += i;
			return 0xFFFD;
		}
		cp = (cp << 6) | (*p & 0x3F);
		p++;
	}

	*src = (const char *)p;
	return cp;
}

/* Return terminal display width for a Unicode codepoint (0, 1, or 2 columns) */
static int
codepoint_width(unsigned long cp)
{
	/* Control characters and format controls (0 width) */
	if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0) || cp == 0x00AD)
		return 0;

	/* Combining marks and non-spacing characters (0 width) */
	if ((cp >= 0x0300 && cp <= 0x036F) ||
	    (cp >= 0x0483 && cp <= 0x0489) ||
	    (cp >= 0x0591 && cp <= 0x05BD) || cp == 0x05BF ||
	    (cp >= 0x05C1 && cp <= 0x05C2) || (cp >= 0x05C4 && cp <= 0x05C5) || cp == 0x05C7 ||
	    (cp >= 0x0610 && cp <= 0x061A) || (cp >= 0x064B && cp <= 0x065F) || cp == 0x0670 ||
	    (cp >= 0x06D6 && cp <= 0x06DC) || (cp >= 0x06DF && cp <= 0x06E4) ||
	    (cp >= 0x06E7 && cp <= 0x06E8) || (cp >= 0x06EA && cp <= 0x06ED) ||
	    cp == 0x0711 ||
	    (cp >= 0x0730 && cp <= 0x074A) ||
	    (cp >= 0x0901 && cp <= 0x0903) || (cp >= 0x093C && cp <= 0x094F) ||
	    (cp >= 0x1AB0 && cp <= 0x1AFF) ||
	    (cp >= 0x1DC0 && cp <= 0x1DFF) ||
	    (cp >= 0x200B && cp <= 0x200F) ||
	    (cp >= 0x2028 && cp <= 0x202E) ||
	    (cp >= 0x2060 && cp <= 0x206F) ||
	    (cp >= 0x20D0 && cp <= 0x20FF) ||
	    (cp >= 0xFE00 && cp <= 0xFE0F) ||
	    (cp >= 0xFE20 && cp <= 0xFE2F) ||
	    cp == 0xFEFF ||
	    (cp >= 0xE0100 && cp <= 0xE01EF)) {
		return 0;
	}

	/* East Asian Wide / Fullwidth characters and Emojis (2 columns) */
	if ((cp >= 0x1100 && cp <= 0x115F) ||
	    (cp == 0x2329 || cp == 0x232A) ||
	    (cp >= 0x2E80 && cp <= 0x303E) ||
	    (cp >= 0x3040 && cp <= 0xA4CF) ||
	    (cp >= 0xAC00 && cp <= 0xD7A3) ||
	    (cp >= 0xF900 && cp <= 0xFAFF) ||
	    (cp >= 0xFE10 && cp <= 0xFE19) ||
	    (cp >= 0xFE30 && cp <= 0xFE6F) ||
	    (cp >= 0xFF01 && cp <= 0xFF60) ||
	    (cp >= 0xFFE0 && cp <= 0xFFE6) ||
	    (cp >= 0x1F300 && cp <= 0x1F64F) ||
	    (cp >= 0x1F680 && cp <= 0x1F6FF) ||
	    (cp >= 0x1F900 && cp <= 0x1F9FF) ||
	    (cp >= 0x20000 && cp <= 0x3FFFD)) {
		return 2;
	}

	return 1;
}

/* Calculate display column width of UTF-8 string */
static size_t
utf8_width(const char *s)
{
	size_t width = 0;
	unsigned long cp;

	if (!s)
		return 0;

	while (*s) {
		cp = utf8_next_codepoint(&s);
		width += codepoint_width(cp);
	}
	return width;
}

/* Trim leading and trailing whitespace from string */
static void
trim_whitespace(char *s)
{
	char *start, *end;

	if (!s || !*s)
		return;

	start = s;
	while (*start && isspace((unsigned char)*start))
		start++;

	end = s + strlen(s) - 1;
	while (end > start && isspace((unsigned char)*end)) {
		*end = '\0';
		end--;
	}

	if (start > s)
		memmove(s, start, strlen(start) + 1);
}

struct table *
table_create(void)
{
	struct table *t = calloc(1, sizeof(*t));
	if (!t) {
		fprintf(stderr, "unipaste: memory allocation failed\n");
		exit(1);
	}
	return t;
}

void
table_free(struct table *t)
{
	int r, c;

	if (!t)
		return;

	for (r = 0; r < t->num_rows && r < MAX_TABLE_ROWS; r++) {
		for (c = 0; c < t->num_cols && c < MAX_TABLE_COLS; c++) {
			if (t->cells[r][c]) {
				free(t->cells[r][c]->text);
				free(t->cells[r][c]);
				t->cells[r][c] = NULL;
			}
		}
	}
	free(t);
}

void
table_add_row(struct table *t)
{
	if (!t || t->num_rows >= MAX_TABLE_ROWS)
		return;

	t->num_rows++;
	t->current_col = 0;
}

void
table_add_cell(struct table *t, const char *text, int is_header)
{
	struct table_cell *cell;
	char *cleaned;

	if (!t || t->num_rows == 0 || t->current_col >= MAX_TABLE_COLS)
		return;

	cleaned = xstrdup(text ? text : "");
	trim_whitespace(cleaned);

	cell = malloc(sizeof(*cell));
	if (!cell) {
		fprintf(stderr, "unipaste: memory allocation failed\n");
		exit(1);
	}
	cell->text = cleaned;
	cell->is_header = is_header;
	cell->colspan = 1;
	cell->rowspan = 1;

	t->cells[t->num_rows - 1][t->current_col] = cell;
	t->current_col++;
	if (t->current_col > t->num_cols)
		t->num_cols = t->current_col;
}

/* Calculate column widths */
static void
table_calculate_widths(struct table *t)
{
	int r, c;
	size_t w;

	for (c = 0; c < t->num_cols && c < MAX_TABLE_COLS; c++)
		t->col_widths[c] = 3; /* minimum width */

	for (r = 0; r < t->num_rows && r < MAX_TABLE_ROWS; r++) {
		for (c = 0; c < t->num_cols && c < MAX_TABLE_COLS; c++) {
			if (t->cells[r][c] && t->cells[r][c]->text) {
				w = utf8_width(t->cells[r][c]->text);
				if ((int)w > t->col_widths[c])
					t->col_widths[c] = (int)w;
			}
		}
	}
}

/* Render horizontal separator for ASCII grid table */
static void
render_grid_sep(struct strbuf *out, const struct table *t, char left, char mid, char right, char fill)
{
	int c, i;

	strbuf_putc(out, left);
	for (c = 0; c < t->num_cols; c++) {
		for (i = 0; i < t->col_widths[c] + 2; i++)
			strbuf_putc(out, fill);
		strbuf_putc(out, (c == t->num_cols - 1) ? right : mid);
	}
	strbuf_puts(out, "\n");
}

/* Render ASCII / Unicode Grid Table */
static void
render_grid(const struct table *t, struct strbuf *out, int unicode)
{
	int r, c, i, pad;
	const char *txt;
	size_t w;

	if (unicode) {
		/* Unicode table box-drawing */
		strbuf_puts(out, "┌");
		for (c = 0; c < t->num_cols; c++) {
			for (i = 0; i < t->col_widths[c] + 2; i++)
				strbuf_puts(out, "─");
			strbuf_puts(out, (c == t->num_cols - 1) ? "┐\n" : "┬");
		}

		for (r = 0; r < t->num_rows; r++) {
			strbuf_puts(out, "│");
			for (c = 0; c < t->num_cols; c++) {
				txt = (t->cells[r][c] && t->cells[r][c]->text) ? t->cells[r][c]->text : "";
				w = utf8_width(txt);
				strbuf_puts(out, " ");
				strbuf_puts(out, txt);
				pad = t->col_widths[c] - (int)w + 1;
				for (i = 0; i < pad; i++)
					strbuf_puts(out, " ");
				strbuf_puts(out, "│");
			}
			strbuf_puts(out, "\n");

			if (r == 0 && t->num_rows > 1) {
				strbuf_puts(out, "├");
				for (c = 0; c < t->num_cols; c++) {
					for (i = 0; i < t->col_widths[c] + 2; i++)
						strbuf_puts(out, "─");
					strbuf_puts(out, (c == t->num_cols - 1) ? "┤\n" : "┼");
				}
			}
		}

		strbuf_puts(out, "└");
		for (c = 0; c < t->num_cols; c++) {
			for (i = 0; i < t->col_widths[c] + 2; i++)
				strbuf_puts(out, "─");
			strbuf_puts(out, (c == t->num_cols - 1) ? "┘\n" : "┴");
		}
	} else {
		/* Standard ASCII grid */
		render_grid_sep(out, t, '+', '+', '+', '-');

		for (r = 0; r < t->num_rows; r++) {
			strbuf_puts(out, "|");
			for (c = 0; c < t->num_cols; c++) {
				txt = (t->cells[r][c] && t->cells[r][c]->text) ? t->cells[r][c]->text : "";
				w = utf8_width(txt);
				strbuf_puts(out, " ");
				strbuf_puts(out, txt);
				pad = t->col_widths[c] - (int)w + 1;
				for (i = 0; i < pad; i++)
					strbuf_putc(out, ' ');
				strbuf_puts(out, "|");
			}
			strbuf_puts(out, "\n");

			if (r == 0 && t->num_rows > 1)
				render_grid_sep(out, t, '+', '+', '+', '-');
		}

		render_grid_sep(out, t, '+', '+', '+', '-');
	}
}

/* Render Markdown Table */
static void
render_markdown(const struct table *t, struct strbuf *out)
{
	int r, c, i, pad;
	const char *txt;
	size_t w;

	for (r = 0; r < t->num_rows; r++) {
		strbuf_puts(out, "|");
		for (c = 0; c < t->num_cols; c++) {
			txt = (t->cells[r][c] && t->cells[r][c]->text) ? t->cells[r][c]->text : "";
			w = utf8_width(txt);
			strbuf_puts(out, " ");
			strbuf_puts(out, txt);
			pad = t->col_widths[c] - (int)w + 1;
			for (i = 0; i < pad; i++)
				strbuf_putc(out, ' ');
			strbuf_puts(out, "|");
		}
		strbuf_puts(out, "\n");

		/* Output header separator row after row 0 */
		if (r == 0) {
			strbuf_puts(out, "|");
			for (c = 0; c < t->num_cols; c++) {
				strbuf_puts(out, " ");
				for (i = 0; i < t->col_widths[c]; i++)
					strbuf_putc(out, '-');
				strbuf_puts(out, " |");
			}
			strbuf_puts(out, "\n");
		}
	}
}

/* Render TSV Table */
static void
render_tsv(const struct table *t, struct strbuf *out)
{
	int r, c;
	const char *txt;

	for (r = 0; r < t->num_rows; r++) {
		for (c = 0; c < t->num_cols; c++) {
			txt = (t->cells[r][c] && t->cells[r][c]->text) ? t->cells[r][c]->text : "";
			strbuf_puts(out, txt);
			if (c < t->num_cols - 1)
				strbuf_putc(out, '\t');
		}
		strbuf_puts(out, "\n");
	}
}

/* Render Simple Aligned Columns */
static void
render_simple(const struct table *t, struct strbuf *out)
{
	int r, c, i, pad;
	const char *txt;
	size_t w;

	for (r = 0; r < t->num_rows; r++) {
		for (c = 0; c < t->num_cols; c++) {
			txt = (t->cells[r][c] && t->cells[r][c]->text) ? t->cells[r][c]->text : "";
			w = utf8_width(txt);
			strbuf_puts(out, txt);
			if (c < t->num_cols - 1) {
				pad = t->col_widths[c] - (int)w + 2;
				for (i = 0; i < pad; i++)
					strbuf_putc(out, ' ');
			}
		}
		strbuf_puts(out, "\n");

		if (r == 0 && t->num_rows > 1) {
			for (c = 0; c < t->num_cols; c++) {
				for (i = 0; i < t->col_widths[c]; i++)
					strbuf_putc(out, '-');
				if (c < t->num_cols - 1)
					strbuf_puts(out, "  ");
			}
			strbuf_puts(out, "\n");
		}
	}
}

/* Render Jira Table: || Header 1 || Header 2 ||\n| Cell 1 | Cell 2 | */
static void
render_jira(const struct table *t, struct strbuf *out)
{
	int r, c;
	const char *txt;
	int is_header_row;

	for (r = 0; r < t->num_rows; r++) {
		is_header_row = (r == 0 && (t->in_header || (t->cells[0][0] && t->cells[0][0]->is_header)));

		if (is_header_row) {
			for (c = 0; c < t->num_cols; c++) {
				txt = (t->cells[r][c] && t->cells[r][c]->text) ? t->cells[r][c]->text : "";
				strbuf_puts(out, "|| ");
				strbuf_puts(out, txt);
				strbuf_puts(out, " ");
			}
			strbuf_puts(out, "||\n");
		} else {
			for (c = 0; c < t->num_cols; c++) {
				txt = (t->cells[r][c] && t->cells[r][c]->text) ? t->cells[r][c]->text : "";
				strbuf_puts(out, "| ");
				strbuf_puts(out, txt);
				strbuf_puts(out, " ");
			}
			strbuf_puts(out, "|\n");
		}
	}
}

void
table_render(struct table *t, struct strbuf *out, const struct config *cfg)
{
	enum table_style style;

	if (!t || t->num_rows == 0 || t->num_cols == 0)
		return;

	table_calculate_widths(t);

	if (cfg->mode == MODE_JIRA) {
		render_jira(t, out);
		return;
	}

	if (cfg->mode == MODE_SLACK) {
		strbuf_puts(out, "```\n");
		render_grid(t, out, cfg->unicode_tables);
		strbuf_puts(out, "```\n");
		return;
	}

	style = cfg->table_style;
	if (style == TABLE_STYLE_AUTO) {
		if (cfg->mode == MODE_MARKDOWN)
			style = TABLE_STYLE_MARKDOWN;
		else
			style = TABLE_STYLE_GRID;
	}

	switch (style) {
	case TABLE_STYLE_MARKDOWN:
		render_markdown(t, out);
		break;
	case TABLE_STYLE_TSV:
		render_tsv(t, out);
		break;
	case TABLE_STYLE_SIMPLE:
		render_simple(t, out);
		break;
	case TABLE_STYLE_GRID:
	default:
		render_grid(t, out, cfg->unicode_tables);
		break;
	}
}
