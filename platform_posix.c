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

static int
write_via_cmd(const char *cmd, const char *data, size_t len)
{
	FILE *fp;
	size_t written;

	if (!cmd || !data || len == 0)
		return -1;
	fp = popen(cmd, "w");
	if (!fp)
		return -1;
	written = fwrite(data, 1, len, fp);
	return (pclose(fp) == 0 && written == len) ? 0 : -1;
}

static int
write_text_fallback(const char *text, size_t len)
{
	return write_via_cmd(get_write_text_cmd(), text, len);
}

/*
 * X11 clipboard holder child. Owns CLIPBOARD and answers SelectionRequest
 * for both UTF8/text/plain and text/html so the rich slot survives a write.
 * Mirrors how xclip -silent keeps the selection alive after the parent exits.
 */
#define X_SELECTION_CLEAR   29
#define X_SELECTION_REQUEST 30
#define X_SELECTION_NOTIFY  31
#define X_CURRENT_TIME      0UL
#define X_PROP_REPLACE      0
#define X_XA_ATOM           4UL
#define X_XA_STRING         31UL
#define X_NO_EVENT_MASK     0L

struct xsel_req {
	int type;
	unsigned long serial;
	int send_event;
	void *display;
	unsigned long owner;
	unsigned long requestor;
	unsigned long selection;
	unsigned long target;
	unsigned long property;
	unsigned long time;
};

struct xsel_ev {
	int type;
	unsigned long serial;
	int send_event;
	void *display;
	unsigned long requestor;
	unsigned long selection;
	unsigned long target;
	unsigned long property;
	unsigned long time;
};

static void
x11_send_sel_notify(void *dpy, int (*pXSendEvent)(void *, unsigned long, int, long, void *),
	int (*pXFlush)(void *), struct xsel_req *req, unsigned long property)
{
	struct xsel_ev ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = X_SELECTION_NOTIFY;
	ev.display = dpy;
	ev.requestor = req->requestor;
	ev.selection = req->selection;
	ev.target = req->target;
	ev.property = property;
	ev.time = req->time;
	pXSendEvent(dpy, req->requestor, 1, X_NO_EVENT_MASK, &ev);
	pXFlush(dpy);
}

static int
x11_offer_html_and_text(const char *text, size_t text_len, const char *html, size_t html_len)
{
	void *libx11 = NULL;
	void *dpy = NULL;
	unsigned long win, clipboard, targets, utf8, text_atom, plain, plain_cs, html_atom, xa_atom, xa_string;
	int screen;
	pid_t child;
	unsigned char evbuf[256];

	void *(*pXOpenDisplay)(const char *);
	int (*pXCloseDisplay)(void *);
	unsigned long (*pXInternAtom)(void *, const char *, int);
	int (*pXDefaultScreen)(void *);
	unsigned long (*pXRootWindow)(void *, int);
	unsigned long (*pXCreateSimpleWindow)(void *, unsigned long, int, int, unsigned int, unsigned int,
		unsigned int, unsigned long, unsigned long);
	int (*pXSetSelectionOwner)(void *, unsigned long, unsigned long, unsigned long);
	unsigned long (*pXGetSelectionOwner)(void *, unsigned long);
	int (*pXChangeProperty)(void *, unsigned long, unsigned long, unsigned long, int, int, const void *, int);
	int (*pXSendEvent)(void *, unsigned long, int, long, void *);
	int (*pXFlush)(void *);
	int (*pXPending)(void *);
	int (*pXNextEvent)(void *, void *);
	int (*pXDestroyWindow)(void *, unsigned long);

	if (!getenv("DISPLAY") || !text || text_len == 0)
		return -1;

	libx11 = dlopen("libX11.so.6", RTLD_LAZY);
	if (!libx11)
		return -1;

	memcpy(&pXOpenDisplay, (void *[]){ dlsym(libx11, "XOpenDisplay") }, sizeof(pXOpenDisplay));
	memcpy(&pXCloseDisplay, (void *[]){ dlsym(libx11, "XCloseDisplay") }, sizeof(pXCloseDisplay));
	memcpy(&pXInternAtom, (void *[]){ dlsym(libx11, "XInternAtom") }, sizeof(pXInternAtom));
	memcpy(&pXDefaultScreen, (void *[]){ dlsym(libx11, "XDefaultScreen") }, sizeof(pXDefaultScreen));
	memcpy(&pXRootWindow, (void *[]){ dlsym(libx11, "XRootWindow") }, sizeof(pXRootWindow));
	memcpy(&pXCreateSimpleWindow, (void *[]){ dlsym(libx11, "XCreateSimpleWindow") }, sizeof(pXCreateSimpleWindow));
	memcpy(&pXSetSelectionOwner, (void *[]){ dlsym(libx11, "XSetSelectionOwner") }, sizeof(pXSetSelectionOwner));
	memcpy(&pXGetSelectionOwner, (void *[]){ dlsym(libx11, "XGetSelectionOwner") }, sizeof(pXGetSelectionOwner));
	memcpy(&pXChangeProperty, (void *[]){ dlsym(libx11, "XChangeProperty") }, sizeof(pXChangeProperty));
	memcpy(&pXSendEvent, (void *[]){ dlsym(libx11, "XSendEvent") }, sizeof(pXSendEvent));
	memcpy(&pXFlush, (void *[]){ dlsym(libx11, "XFlush") }, sizeof(pXFlush));
	memcpy(&pXPending, (void *[]){ dlsym(libx11, "XPending") }, sizeof(pXPending));
	memcpy(&pXNextEvent, (void *[]){ dlsym(libx11, "XNextEvent") }, sizeof(pXNextEvent));
	memcpy(&pXDestroyWindow, (void *[]){ dlsym(libx11, "XDestroyWindow") }, sizeof(pXDestroyWindow));

	if (!pXOpenDisplay || !pXCloseDisplay || !pXInternAtom || !pXDefaultScreen || !pXRootWindow ||
	    !pXCreateSimpleWindow || !pXSetSelectionOwner || !pXGetSelectionOwner || !pXChangeProperty ||
	    !pXSendEvent || !pXFlush || !pXPending || !pXNextEvent || !pXDestroyWindow) {
		dlclose(libx11);
		return -1;
	}

	child = fork();
	if (child < 0) {
		dlclose(libx11);
		return -1;
	}
	if (child > 0) {
		/* Parent: holder lives until another owner takes CLIPBOARD. */
		dlclose(libx11);
		return 0;
	}

	/* Child clipboard owner. */
	signal(SIGINT, SIG_IGN);
	signal(SIGTERM, SIG_DFL);
	if (setsid() < 0) {
		/* still try to hold the selection */
	}

	dpy = pXOpenDisplay(NULL);
	if (!dpy)
		_exit(1);

	screen = pXDefaultScreen(dpy);
	win = pXCreateSimpleWindow(dpy, pXRootWindow(dpy, screen), 0, 0, 1, 1, 0, 0, 0);
	clipboard = pXInternAtom(dpy, "CLIPBOARD", 0);
	targets = pXInternAtom(dpy, "TARGETS", 0);
	utf8 = pXInternAtom(dpy, "UTF8_STRING", 0);
	text_atom = pXInternAtom(dpy, "TEXT", 0);
	plain = pXInternAtom(dpy, "text/plain", 0);
	plain_cs = pXInternAtom(dpy, "text/plain;charset=utf-8", 0);
	html_atom = pXInternAtom(dpy, "text/html", 0);
	xa_atom = X_XA_ATOM;
	xa_string = X_XA_STRING;

	pXSetSelectionOwner(dpy, clipboard, win, X_CURRENT_TIME);
	pXFlush(dpy);
	if (pXGetSelectionOwner(dpy, clipboard) != win) {
		pXDestroyWindow(dpy, win);
		pXCloseDisplay(dpy);
		_exit(1);
	}

	while (1) {
		struct xsel_req *req;

		pXNextEvent(dpy, evbuf);
		req = (struct xsel_req *)evbuf;

		if (req->type == X_SELECTION_CLEAR)
			break;

		if (req->type != X_SELECTION_REQUEST || req->owner != win)
			continue;

		if (req->target == targets) {
			unsigned long list[8];
			int n = 0;

			list[n++] = targets;
			list[n++] = utf8;
			list[n++] = text_atom;
			list[n++] = plain;
			list[n++] = plain_cs;
			list[n++] = xa_string;
			if (html && html_len > 0)
				list[n++] = html_atom;
			pXChangeProperty(dpy, req->requestor,
				req->property ? req->property : targets,
				xa_atom, 32, X_PROP_REPLACE, list, n);
			x11_send_sel_notify(dpy, pXSendEvent, pXFlush, req,
				req->property ? req->property : targets);
		} else if (req->target == utf8 || req->target == text_atom ||
		    req->target == plain || req->target == plain_cs || req->target == xa_string) {
			unsigned long prop = req->property ? req->property : req->target;
			pXChangeProperty(dpy, req->requestor, prop, req->target == xa_string ? xa_string : utf8,
				8, X_PROP_REPLACE, text, (int)text_len);
			x11_send_sel_notify(dpy, pXSendEvent, pXFlush, req, prop);
		} else if (html && html_len > 0 && req->target == html_atom) {
			unsigned long prop = req->property ? req->property : req->target;
			pXChangeProperty(dpy, req->requestor, prop, html_atom,
				8, X_PROP_REPLACE, html, (int)html_len);
			x11_send_sel_notify(dpy, pXSendEvent, pXFlush, req, prop);
		} else {
			x11_send_sel_notify(dpy, pXSendEvent, pXFlush, req, 0);
		}
	}

	pXDestroyWindow(dpy, win);
	pXCloseDisplay(dpy);
	_exit(0);
	return 0;
}

static int
clipboard_write_text_and_preserve_html(const char *text, size_t text_len, const char *html, size_t html_len)
{
	int wrote = -1;

	if (!text || text_len == 0)
		return 0;

	/*
	 * Wayland native clients read the Wayland clipboard (single type via
	 * wl-copy). If DISPLAY is also set, own the X11 CLIPBOARD with both
	 * text and HTML so XWayland / xclip clients keep the rich slot.
	 */
	if (getenv("WAYLAND_DISPLAY"))
		wrote = write_via_cmd("wl-copy -t text/plain", text, text_len);

	if (getenv("DISPLAY") && html && html_len > 0) {
		if (x11_offer_html_and_text(text, text_len, html, html_len) == 0)
			wrote = 0;
	}

	if (wrote == 0)
		return 0;

	return write_text_fallback(text, text_len);
}

int
clipboard_write_text(const char *text, size_t len)
{
	return clipboard_write_text_and_preserve_html(text, len, NULL, 0);
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

	if (ret == 0 && out_sb.len > 0)
		clipboard_write_text_and_preserve_html(out_sb.data, out_sb.len, html, html_len);

	free(html);
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
			clipboard_write_text_and_preserve_html(out_sb.data, out_sb.len, curr_html, curr_len);
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
		pid_t child;
		int st = 0;

		child = fork();
		if (child < 0)
			return -1;
		if (child == 0) {
			execlp("clipnotify", "clipnotify", (char *)NULL);
			_exit(127);
		}

		while (running) {
			pid_t w = waitpid(child, &st, 0);
			if (w < 0 && errno == EINTR)
				continue;
			break;
		}

		if (!running) {
			kill(child, SIGTERM);
			waitpid(child, NULL, 0);
			break;
		}
		if (WIFEXITED(st) && WEXITSTATUS(st) == 127)
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
