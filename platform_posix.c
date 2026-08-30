/* See LICENSE file for copyright and license details. */
#if !defined(_WIN32) && !defined(__APPLE__)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/file.h>
#include "clipbridge.h"
#include "unipaste.h"

static volatile sig_atomic_t running = 1;

static void
sig_handler(int sig)
{
	(void)sig;
	running = 0;
}

/* Determine the available clipboard read command for HTML */
static const char *
get_read_html_cmd(void)
{
	if (getenv("WAYLAND_DISPLAY"))
		return "wl-paste -t text/html 2>/dev/null";
	else
		return "xclip -selection clipboard -t text/html -o 2>/dev/null || xsel -b -t text/html 2>/dev/null";
}

/* Determine the available clipboard write command for plain text */
static const char *
get_write_text_cmd(void)
{
	if (getenv("WAYLAND_DISPLAY"))
		return "wl-copy -t text/plain";
	else
		return "xclip -selection clipboard || xsel -b -i";
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
	struct strbuf out_sb;
	int ret;

	if (clipboard_read_html(&html, &html_len) != 0 || !html || html_len == 0) {
		return 1; /* No rich HTML currently on clipboard */
	}

	strbuf_init(&out_sb, html_len * 2);
	ret = unipaste_process_to_strbuf(html, html_len, &out_sb, cfg);
	free(html);

	if (ret == 0 && out_sb.len > 0) {
		clipboard_write_text(out_sb.data, out_sb.len);
		strbuf_free(&out_sb);
		return 0;
	}

	strbuf_free(&out_sb);
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
clipboard_paste_active(const struct config *cfg)
{
	int ret = clipboard_sync_once(cfg);
	if (ret != 0)
		return ret;

	/* Synthesize paste keystroke using ydotool, wtype, or xdotool */
	if (system("which ydotool >/dev/null 2>&1") == 0) {
		return system("ydotool key 29:1 47:1 47:0 29:0");
	} else if (system("which wtype >/dev/null 2>&1") == 0) {
		return system("wtype -M ctrl -k v -m ctrl");
	} else if (system("which xdotool >/dev/null 2>&1") == 0) {
		return system("xdotool key --clearmodifiers ctrl+v");
	}
	return 0;
}

int
clipboard_watch(const struct config *cfg)
{
	char *last_html = NULL;
	size_t last_len = 0;
	char *curr_html = NULL;
	size_t curr_len = 0;
	struct sigaction sa;
	int lock_fd;

	/* Singleton check on POSIX */
	lock_fd = open("/tmp/clipbridge.lock", O_RDWR | O_CREAT, 0666);
	if (lock_fd >= 0) {
		if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
			fprintf(stderr, "clipbridge: another instance is already running.\n");
			close(lock_fd);
			return 0;
		}
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	printf("clipbridge: monitoring Linux/BSD clipboard (press Ctrl+C to stop)...\n");
	fflush(stdout);

	while (running) {
		if (clipboard_read_html(&curr_html, &curr_len) == 0 && curr_html && curr_len > 0) {
			if (last_html == NULL || last_len != curr_len || memcmp(last_html, curr_html, curr_len) != 0) {
				struct strbuf out_sb;
				strbuf_init(&out_sb, curr_len * 2);

				if (unipaste_process_to_strbuf(curr_html, curr_len, &out_sb, cfg) == 0) {
					if (out_sb.len > 0) {
						clipboard_write_text(out_sb.data, out_sb.len);
						printf("clipbridge: [synced] formatted %zu bytes of rich text -> plain text\n", curr_len);
						fflush(stdout);
					}
				}
				strbuf_free(&out_sb);

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
	printf("\nclipbridge: stopped cleanly.\n");
	return 0;
}

#else
typedef int iso_c_dummy_clipbridge_posix;
#endif /* !defined(_WIN32) && !defined(__APPLE__) */
