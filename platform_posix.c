/* See LICENSE file for copyright and license details. */
#if !defined(_WIN32) && !defined(__APPLE__)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/file.h>
#include <sys/select.h>
#include <sys/wait.h>
#include "clipbridge.h"
#include "unipaste.h"
#include "i18n.h"

static volatile sig_atomic_t running = 1;

static void
sig_handler(int sig)
{
	(void)sig;
	running = 0;
}

/* Check if clipboard is marked private/concealed by password managers on Linux */
static int
is_posix_clipboard_ignored(void)
{
	FILE *fp;
	char line[256];
	int ignored = 0;

	if (getenv("WAYLAND_DISPLAY")) {
		fp = popen("wl-paste --list-types 2>/dev/null", "r");
		if (fp) {
			while (fgets(line, sizeof(line), fp)) {
				if (strstr(line, "password") || strstr(line, "Password") ||
				    strstr(line, "1password") || strstr(line, "keepass") ||
				    strstr(line, "concealed") || strstr(line, "x-kde-passwordManagerHint")) {
					ignored = 1;
					break;
				}
			}
			pclose(fp);
		}
	} else {
		fp = popen("xclip -selection clipboard -t TARGETS -o 2>/dev/null", "r");
		if (fp) {
			while (fgets(line, sizeof(line), fp)) {
				if (strstr(line, "password") || strstr(line, "Password") ||
				    strstr(line, "1password") || strstr(line, "keepass") ||
				    strstr(line, "concealed") || strstr(line, "x-kde-passwordManagerHint")) {
					ignored = 1;
					break;
				}
			}
			pclose(fp);
		}
	}

	return ignored;
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

	if (is_posix_clipboard_ignored())
		return -1;

	cmd = get_read_html_cmd();
	fp = popen(cmd, "r");
	if (!fp)
		return -1;

	strbuf_init(&sb, 4096);
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		strbuf_append(&sb, buf, n);
		if (sb.len > 10 * 1024 * 1024) { /* Cap at 10 MB */
			break;
		}
	}

	pclose(fp);

	if (sb.len == 0 || sb.len > 10 * 1024 * 1024) {
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

static void
sync_clipboard_if_changed(const struct config *cfg, char **last_html, size_t *last_len)
{
	char *curr_html = NULL;
	size_t curr_len = 0;

	/* Apps often publish text then HTML a few milliseconds apart. */
	usleep(40000);

	if (clipboard_read_html(&curr_html, &curr_len) != 0 || !curr_html || curr_len == 0)
		return;

	if (*last_html && *last_len == curr_len && memcmp(*last_html, curr_html, curr_len) == 0) {
		free(curr_html);
		return;
	}

	{
		struct strbuf out_sb;
		strbuf_init(&out_sb, curr_len * 2);
		if (unipaste_process_to_strbuf(curr_html, curr_len, &out_sb, cfg) == 0 && out_sb.len > 0) {
			clipboard_write_text(out_sb.data, out_sb.len);
			printf("clipbridge: [synced] formatted %zu bytes of rich text -> plain text\n", curr_len);
			fflush(stdout);
		}
		strbuf_free(&out_sb);
	}

	free(*last_html);
	*last_html = curr_html;
	*last_len = curr_len;
}

static int
cmd_exists(const char *name)
{
	char cmd[128];
	int st;

	snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", name);
	st = system(cmd);
	return st == 0;
}

static int
open_watch_pipe(const char *shell_cmd, pid_t *child_pid)
{
	int fds[2];
	pid_t pid;

	if (pipe(fds) != 0)
		return -1;

	pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return -1;
	}

	if (pid == 0) {
		close(fds[0]);
		if (dup2(fds[1], STDOUT_FILENO) < 0)
			_exit(127);
		close(fds[1]);
		execl("/bin/sh", "sh", "-c", shell_cmd, (char *)NULL);
		_exit(127);
	}

	close(fds[1]);
	*child_pid = pid;
	return fds[0];
}

static void
stop_watch_child(pid_t pid, int fd)
{
	if (fd >= 0)
		close(fd);
	if (pid > 0) {
		kill(pid, SIGTERM);
		waitpid(pid, NULL, 0);
	}
}

static int
wait_for_byte(int fd)
{
	fd_set rfds;
	char buf[256];
	int n, r;

	while (running) {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		r = select(fd + 1, &rfds, NULL, NULL, NULL);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (!running)
			return 0;
		if (r > 0 && FD_ISSET(fd, &rfds)) {
			n = read(fd, buf, sizeof(buf));
			if (n <= 0)
				return -1;
			return 1;
		}
	}
	return 0;
}

/* Wayland: wl-paste --watch is the compositor data-control event path. */
static int
watch_wayland(const struct config *cfg, char **last_html, size_t *last_len)
{
	pid_t pid = -1;
	int fd;
	int ev;

	if (!getenv("WAYLAND_DISPLAY") || !cmd_exists("wl-paste"))
		return -1;

	fd = open_watch_pipe("exec wl-paste --watch printf '.\\n'", &pid);
	if (fd < 0)
		return -1;

	printf("clipbridge: watching Wayland clipboard via wl-paste --watch\n");
	fflush(stdout);

	/* Catch whatever is already on the board, then wait for events. */
	sync_clipboard_if_changed(cfg, last_html, last_len);

	while (running) {
		ev = wait_for_byte(fd);
		if (ev < 0)
			break;
		if (ev > 0)
			sync_clipboard_if_changed(cfg, last_html, last_len);
	}

	stop_watch_child(pid, fd);
	return running ? -1 : 0;
}

/*
 * X11: talk to libX11/libXfixes through dlopen so the binary still
 * links with zero extra build dependencies. Layout of the first
 * Display fields (ext_data, next, fd) has been stable for decades.
 */
struct xdisplay_min {
	void *ext_data;
	void *next;
	int fd;
};

#define XFIXES_SET_OWNER_MASK        (1L << 0)
#define XFIXES_WINDOW_DESTROY_MASK   (1L << 1)
#define XFIXES_CLIENT_CLOSE_MASK     (1L << 2)

static void *
load_sym(void *lib, const char *name)
{
	return dlsym(lib, name);
}

static int
watch_x11_xfixes(const struct config *cfg, char **last_html, size_t *last_len)
{
	void *libx11 = NULL;
	void *libxfixes = NULL;
	void *dpy = NULL;
	int event_base = 0, error_base = 0;
	int fd, r;
	unsigned long clipboard_atom;
	fd_set rfds;
	unsigned char ev[256];

	void *(*pXOpenDisplay)(const char *);
	int (*pXCloseDisplay)(void *);
	unsigned long (*pXInternAtom)(void *, const char *, int);
	int (*pXDefaultScreen)(void *);
	unsigned long (*pXRootWindow)(void *, int);
	int (*pXPending)(void *);
	int (*pXNextEvent)(void *, void *);
	int (*pXFixesQueryExtension)(void *, int *, int *);
	void (*pXFixesSelectSelectionInput)(void *, unsigned long, unsigned long, unsigned long);

	if (!getenv("DISPLAY") || getenv("WAYLAND_DISPLAY"))
		return -1;

	libx11 = dlopen("libX11.so.6", RTLD_LAZY);
	libxfixes = dlopen("libXfixes.so.3", RTLD_LAZY);
	if (!libx11 || !libxfixes)
		goto fail_open;

	memcpy(&pXOpenDisplay, (void *[]){ load_sym(libx11, "XOpenDisplay") }, sizeof(pXOpenDisplay));
	memcpy(&pXCloseDisplay, (void *[]){ load_sym(libx11, "XCloseDisplay") }, sizeof(pXCloseDisplay));
	memcpy(&pXInternAtom, (void *[]){ load_sym(libx11, "XInternAtom") }, sizeof(pXInternAtom));
	memcpy(&pXDefaultScreen, (void *[]){ load_sym(libx11, "XDefaultScreen") }, sizeof(pXDefaultScreen));
	memcpy(&pXRootWindow, (void *[]){ load_sym(libx11, "XRootWindow") }, sizeof(pXRootWindow));
	memcpy(&pXPending, (void *[]){ load_sym(libx11, "XPending") }, sizeof(pXPending));
	memcpy(&pXNextEvent, (void *[]){ load_sym(libx11, "XNextEvent") }, sizeof(pXNextEvent));
	memcpy(&pXFixesQueryExtension, (void *[]){ load_sym(libxfixes, "XFixesQueryExtension") }, sizeof(pXFixesQueryExtension));
	memcpy(&pXFixesSelectSelectionInput, (void *[]){ load_sym(libxfixes, "XFixesSelectSelectionInput") },
		sizeof(pXFixesSelectSelectionInput));

	if (!pXOpenDisplay || !pXCloseDisplay || !pXInternAtom || !pXDefaultScreen ||
	    !pXRootWindow || !pXPending || !pXNextEvent ||
	    !pXFixesQueryExtension || !pXFixesSelectSelectionInput)
		goto fail_open;

	dpy = pXOpenDisplay(NULL);
	if (!dpy)
		goto fail_open;

	if (!pXFixesQueryExtension(dpy, &event_base, &error_base)) {
		pXCloseDisplay(dpy);
		goto fail_open;
	}

	clipboard_atom = pXInternAtom(dpy, "CLIPBOARD", 0);
	pXFixesSelectSelectionInput(dpy,
		pXRootWindow(dpy, pXDefaultScreen(dpy)),
		clipboard_atom,
		XFIXES_SET_OWNER_MASK | XFIXES_WINDOW_DESTROY_MASK | XFIXES_CLIENT_CLOSE_MASK);

	fd = ((struct xdisplay_min *)dpy)->fd;
	if (fd < 0) {
		pXCloseDisplay(dpy);
		goto fail_open;
	}

	printf("clipbridge: watching X11 CLIPBOARD via XFixes\n");
	fflush(stdout);

	sync_clipboard_if_changed(cfg, last_html, last_len);

	while (running) {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		r = select(fd + 1, &rfds, NULL, NULL, NULL);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (!running)
			break;
		while (pXPending(dpy))
			pXNextEvent(dpy, ev);
		sync_clipboard_if_changed(cfg, last_html, last_len);
	}

	pXCloseDisplay(dpy);
	dlclose(libxfixes);
	dlclose(libx11);
	return 0;

fail_open:
	if (libxfixes)
		dlclose(libxfixes);
	if (libx11)
		dlclose(libx11);
	return -1;
}

/* Optional helper used by many X11 clipboard tools. */
static int
watch_clipnotify(const struct config *cfg, char **last_html, size_t *last_len)
{
	if (getenv("WAYLAND_DISPLAY") || !cmd_exists("clipnotify"))
		return -1;

	printf("clipbridge: watching X11 clipboard via clipnotify\n");
	fflush(stdout);

	sync_clipboard_if_changed(cfg, last_html, last_len);

	while (running) {
		int st = system("clipnotify");
		if (!running)
			break;
		if (st != 0)
			return -1;
		sync_clipboard_if_changed(cfg, last_html, last_len);
	}
	return 0;
}

static void
watch_poll_fallback(const struct config *cfg, char **last_html, size_t *last_len)
{
	printf("clipbridge: no event source available, polling every 250ms\n");
	fflush(stdout);

	while (running) {
		sync_clipboard_if_changed(cfg, last_html, last_len);
		usleep(250000);
	}
}

int
clipboard_watch(const struct config *cfg)
{
	char *last_html = NULL;
	size_t last_len = 0;
	struct sigaction sa;
	int lock_fd;

	/* Singleton check on POSIX with secure 0600 permissions */
	lock_fd = open("/tmp/clipbridge.lock", O_RDWR | O_CREAT, 0600);
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

	i18n_init(LANG_AUTO);

	printf("clipbridge: %s\n", i18n_get(STR_TOOLTIP_ACTIVE));
	fflush(stdout);

	if (watch_wayland(cfg, &last_html, &last_len) != 0 && running) {
		if (watch_x11_xfixes(cfg, &last_html, &last_len) != 0 && running) {
			if (watch_clipnotify(cfg, &last_html, &last_len) != 0 && running)
				watch_poll_fallback(cfg, &last_html, &last_len);
		}
	}

	free(last_html);
	if (lock_fd >= 0)
		close(lock_fd);
	printf("\nclipbridge: stopped cleanly.\n");
	return 0;
}

#else
typedef int iso_c_dummy_clipbridge_posix;
#endif /* !defined(_WIN32) && !defined(__APPLE__) */
