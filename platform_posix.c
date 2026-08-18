/* See LICENSE file for copyright and license details. */
#ifndef _WIN32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "clipbridge.h"

/* Determine the available clipboard read command for HTML */
static const char *
get_read_html_cmd(void)
{
#ifdef __APPLE__
	return "pbpaste -Prefer html 2>/dev/null";
#else
	if (getenv("WAYLAND_DISPLAY"))
		return "wl-paste -t text/html 2>/dev/null";
	else
		return "xclip -selection clipboard -t text/html -o 2>/dev/null || xsel -b -t text/html 2>/dev/null";
#endif
}

/* Determine the available clipboard write command for plain text */
static const char *
get_write_text_cmd(void)
{
#ifdef __APPLE__
	return "pbcopy";
#else
	if (getenv("WAYLAND_DISPLAY"))
		return "wl-copy -t text/plain";
	else
		return "xclip -selection clipboard || xsel -b -i";
#endif
}

int
clipboard_read_html(char **out_html, size_t *out_len)
{
	const char *cmd;
	FILE *fp;
	struct strbuf sb;
	char buf[4096];
	size_t n;

	*out_html = NULL;
	*out_len = 0;

	cmd = get_read_html_cmd();
	fp = popen(cmd, "r");
	if (!fp)
		return -1;

	strbuf_init(&sb, 4096);
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		strbuf_append(&sb, buf, n);
	}

	pclose(fp);

	if (sb.len == 0) {
		strbuf_free(&sb);
		return -1;
	}

	*out_html = sb.data;
	*out_len = sb.len;
	return 0;
}

int
clipboard_write_text(const char *text, size_t len)
{
	const char *cmd;
	FILE *fp;
	size_t written;

	if (!text || len == 0)
		return 0;

	cmd = get_write_text_cmd();
	fp = popen(cmd, "w");
	if (!fp)
		return -1;

	written = fwrite(text, 1, len, fp);
	pclose(fp);

	return (written == len) ? 0 : -1;
}

int
clipboard_sync_once(const struct config *cfg)
{
	char *html = NULL;
	size_t html_len = 0;
	char *tmp_buf = NULL;
	size_t tmp_len = 0;
	FILE *mem_fp;
	int ret;

	if (clipboard_read_html(&html, &html_len) != 0 || !html || html_len == 0) {
		return 1; /* No rich HTML currently on clipboard */
	}

	mem_fp = open_memstream(&tmp_buf, &tmp_len);
	if (!mem_fp) {
		free(html);
		return -1;
	}

	ret = unipaste_process_string(html, html_len, mem_fp, cfg);
	fclose(mem_fp);
	free(html);

	if (ret == 0 && tmp_buf && tmp_len > 0) {
		clipboard_write_text(tmp_buf, tmp_len);
		free(tmp_buf);
		return 0;
	}

	free(tmp_buf);
	return ret;
}

int
clipboard_paste_stdout(const struct config *cfg)
{
	char *html = NULL;
	size_t html_len = 0;
	int ret;

	if (clipboard_read_html(&html, &html_len) != 0 || !html || html_len == 0) {
		fprintf(stderr, "clipbridge: no rich text/html found on clipboard\n");
		return 1;
	}

	ret = unipaste_process_string(html, html_len, stdout, cfg);
	free(html);
	return ret;
}

int
clipboard_watch(const struct config *cfg)
{
	char *last_html = NULL;
	size_t last_len = 0;
	char *curr_html = NULL;
	size_t curr_len = 0;

	printf("clipbridge: monitoring clipboard (press Ctrl+C to stop)...\n");
	fflush(stdout);

	while (1) {
		if (clipboard_read_html(&curr_html, &curr_len) == 0 && curr_html && curr_len > 0) {
			if (last_html == NULL || last_len != curr_len || memcmp(last_html, curr_html, curr_len) != 0) {
				char *tmp_buf = NULL;
				size_t tmp_len = 0;
				FILE *mem_fp = open_memstream(&tmp_buf, &tmp_len);

				if (mem_fp) {
					if (unipaste_process_string(curr_html, curr_len, mem_fp, cfg) == 0) {
						fclose(mem_fp);
						if (tmp_buf && tmp_len > 0) {
							clipboard_write_text(tmp_buf, tmp_len);
							printf("clipbridge: [synced] formatted %zu bytes of rich text -> plain text\n", curr_len);
							fflush(stdout);
						}
					} else {
						fclose(mem_fp);
					}
					free(tmp_buf);
				}

				free(last_html);
				last_html = curr_html;
				last_len = curr_len;
				curr_html = NULL;
			} else {
				free(curr_html);
				curr_html = NULL;
			}
		}

		usleep(250000); /* 250ms interval */
	}

	free(last_html);
	return 0;
}

#else
typedef int iso_c_dummy_clipbridge_posix;
#endif /* !_WIN32 */
