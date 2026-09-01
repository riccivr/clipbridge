/* See LICENSE file for copyright and license details. */
#ifndef UNIPASTE_H
#define UNIPASTE_H

#include <stddef.h>
#include <stdio.h>

#define MAX_LIST_DEPTH 32
#define MAX_TABLE_COLS 64
#define MAX_TABLE_ROWS 1024
#define MAX_TAG_NAME   64
#define MAX_ATTR_VAL   1024

enum output_mode {
	MODE_PLAIN = 0,
	MODE_MARKDOWN,
	MODE_SLACK,
	MODE_JIRA,
	MODE_TERMINAL
};

enum table_style {
	TABLE_STYLE_AUTO = 0,
	TABLE_STYLE_GRID,
	TABLE_STYLE_MARKDOWN,
	TABLE_STYLE_TSV,
	TABLE_STYLE_SIMPLE
};

enum link_style {
	LINK_STYLE_AUTO = 0,
	LINK_STYLE_BRACKET,   /* text (url) */
	LINK_STYLE_INLINE,    /* [text](url) */
	LINK_STYLE_TEXTONLY,  /* text */
	LINK_STYLE_FOOTNOTE   /* text [1] ... [1] url */
};

struct config {
	enum output_mode mode;
	enum table_style table_style;
	enum link_style link_style;
	int wrap_width;
	int crlf;
	int unicode_tables;
	int preserve_empty_lines;
	int keep_tracking; /* 0 = strip tracking params by default, 1 = preserve all */
};

/* Table cell structure */
struct table_cell {
	char *text;
	int is_header;
	int colspan;
	int rowspan;
};

/* Table structure */
struct table {
	struct table_cell *cells[MAX_TABLE_ROWS][MAX_TABLE_COLS];
	int col_widths[MAX_TABLE_COLS];
	int num_rows;
	int num_cols;
	int in_header;
	int current_col;
};

/* String buffer */
struct strbuf {
	char *data;
	size_t len;
	size_t cap;
};

/* Parser state */
struct parser_state {
	const struct config *cfg;
	FILE *out;
	
	/* Buffers */
	struct strbuf outbuf;
	struct strbuf linebuf;
	struct strbuf tagbuf;
	struct strbuf textbuf;
	
	/* Nesting states */
	int list_type[MAX_LIST_DEPTH]; /* 0: none, 1: ul, 2: ol */
	int list_count[MAX_LIST_DEPTH];
	int list_depth;
	
	int quote_depth;
	int pre_depth;
	int code_depth;
	int bold_depth;
	int italic_depth;
	int strike_depth;
	int underline_depth;
	
	/* Link tracking */
	char current_href[MAX_ATTR_VAL];
	int in_link;
	struct strbuf link_text;
	char *footnotes[256];
	int num_footnotes;
	
	/* Heading tracking */
	int heading_level;
	struct strbuf heading_text;
	
	/* Math tracking */
	int in_math;
	int math_display;
	int in_annotation;
	struct strbuf math_text;

	/* Table tracking */
#define MAX_TABLE_DEPTH 8
	struct table *table_stack[MAX_TABLE_DEPTH];
	struct strbuf cell_save[MAX_TABLE_DEPTH];
	int table_depth;
	int pending_colspan;
	
	/* Paragraph / spacing tracking */
	int consecutive_newlines;
	int at_line_start;
	int need_space;
};

/* String buffer functions */
void strbuf_init(struct strbuf *sb, size_t initial_cap);
void strbuf_free(struct strbuf *sb);
void strbuf_reset(struct strbuf *sb);
void strbuf_putc(struct strbuf *sb, char c);
void strbuf_puts(struct strbuf *sb, const char *s);
void strbuf_append(struct strbuf *sb, const char *data, size_t len);

/* Entity decoding */
size_t decode_html_entity(const char *src, char *dest, size_t dest_size);
void decode_html_entities_inplace(char *str);

/* Table functions */
struct table *table_create(void);
void table_free(struct table *t);
void table_add_row(struct table *t);
void table_add_cell(struct table *t, const char *text, int is_header);
void table_add_cell_span(struct table *t, const char *text, int is_header, int colspan);
void table_render(struct table *t, struct strbuf *out, const struct config *cfg);
void table_render_inline(struct table *t, struct strbuf *out);

/* URL utilities */
void url_strip_tracking_inplace(char *url);

/* Core processing */
int unipaste_process_stream(FILE *in, FILE *out, const struct config *cfg);
int unipaste_process_string(const char *input, size_t len, FILE *out, const struct config *cfg);
int unipaste_process_to_strbuf(const char *input, size_t len, struct strbuf *out, const struct config *cfg);

#endif /* UNIPASTE_H */
