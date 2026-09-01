/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "arg.h"
#include "clipbridge.h"
#include "unipaste.h"

char *argv0;

enum bridge_action {
	ACTION_WATCH = 0,
	ACTION_ONCE,
	ACTION_PASTE,
	ACTION_PASTE_ACTIVE
};

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-w1kpruvKh] [-m mode] [-t table] [-l link] [--uninstall]\n", argv0);
	fprintf(stderr, "\nActions:\n");
	fprintf(stderr, "  -w            Watch clipboard continuously and auto-sync (default)\n");
	fprintf(stderr, "  -1            Perform single clipboard synchronization and exit\n");
	fprintf(stderr, "  -k            Paste formatted clipboard directly into active window\n");
	fprintf(stderr, "  -p            Print formatted clipboard content directly to stdout\n");
	fprintf(stderr, "  --uninstall   Uninstall ClipBridge desktop shortcuts and user configuration\n");
	fprintf(stderr, "\nFormatting Options:\n");
	fprintf(stderr, "  -m mode       Output mode: plain (default), markdown, slack, jira, terminal\n");
	fprintf(stderr, "  -t table      Table format: grid (default), markdown, tsv, simple\n");
	fprintf(stderr, "  -l link       Link format: bracket (default), inline, text, footnote\n");
	fprintf(stderr, "  -K            Keep URL tracking & telemetry parameters\n");
	fprintf(stderr, "  -u            Use Unicode box-drawing characters for tables\n");
	fprintf(stderr, "  -r            Emit Windows CRLF (\\r\\n) line endings\n");
	fprintf(stderr, "  -v            Display version information\n");
	fprintf(stderr, "  -h            Display this help message\n");
	exit(1);
}

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>

static void
attach_console_if_needed(int argc)
{
	if (argc > 1) {
		if (AttachConsole(ATTACH_PARENT_PROCESS)) {
			freopen("CONOUT$", "w", stdout);
			freopen("CONOUT$", "w", stderr);
			freopen("CONIN$", "r", stdin);
		}
	}
}

static int
perform_uninstall(void)
{
	wchar_t localAppData[MAX_PATH * 2];
	HKEY hKey;

	HWND existing = FindWindowA("ClipBridgeMonitorClass", "ClipBridge");
	if (existing) {
		PostMessageA(existing, WM_COMMAND, 1015, 0);
		Sleep(400);
	}

	if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, localAppData))) {
		wchar_t shortcutPath[MAX_PATH * 2];
		_snwprintf(shortcutPath, sizeof(shortcutPath) / sizeof(wchar_t), L"%ls\\ClipBridge.lnk", localAppData);
		DeleteFileW(shortcutPath);
	}

	if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
		RegDeleteValueW(hKey, L"ClipBridge");
		RegCloseKey(hKey);
	}

	RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ClipBridge");
	RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\ClipBridge");

	if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
		wchar_t appDir[MAX_PATH * 2];
		_snwprintf(appDir, sizeof(appDir) / sizeof(wchar_t), L"%ls\\ClipBridge", localAppData);
		char cmd[MAX_PATH * 3];
		snprintf(cmd, sizeof(cmd), "/C ping 127.0.0.1 -n 2 > nul & rd /S /Q \"%ls\"", appDir);
		ShellExecuteA(NULL, "open", "cmd.exe", cmd, NULL, SW_HIDE);
	}

	MessageBoxA(NULL, "ClipBridge has been uninstalled successfully.", "ClipBridge Uninstall", MB_OK | MB_ICONINFORMATION);
	return 0;
}
#elif defined(__APPLE__)
static int
perform_uninstall(void)
{
	printf("Uninstalling ClipBridge on macOS...\n");
	system("launchctl unload ~/Library/LaunchAgents/com.riccivr.clipbridge.plist 2>/dev/null || true");
	system("rm -f ~/Library/LaunchAgents/com.riccivr.clipbridge.plist");
	system("defaults delete com.riccivr.clipbridge 2>/dev/null || true");
	system("rm -rf ~/Library/Preferences/com.riccivr.clipbridge.plist");
	system("rm -rf /Applications/ClipBridge.app 2>/dev/null || true");
	printf("ClipBridge uninstalled successfully.\n");
	return 0;
}
#else
static int
perform_uninstall(void)
{
	printf("Uninstalling ClipBridge on Linux...\n");
	system("systemctl --user stop clipbridge.service 2>/dev/null || true");
	system("systemctl --user disable clipbridge.service 2>/dev/null || true");
	system("rm -f ~/.config/systemd/user/clipbridge.service");
	system("rm -f ~/.local/share/applications/clipbridge.desktop");
	system("rm -f ~/.local/share/icons/hicolor/scalable/apps/clipbridge.svg");
	system("rm -f ~/.local/share/pixmaps/clipbridge.png");
	system("rm -rf ~/.config/clipbridge");
	printf("ClipBridge desktop shortcuts and user configuration uninstalled.\n");
	return 0;
}
#endif

int
main(int argc, char *argv[])
{
	struct config cfg;
	enum bridge_action action = ACTION_WATCH;
	char *arg;

#ifdef _WIN32
	attach_console_if_needed(argc);
#endif

	/* Handle --uninstall flag before getopt / arg parsing */
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--uninstall") == 0 || (strcmp(argv[i], "-u") == 0 && (i + 1 < argc && strcmp(argv[i+1], "ninstall") == 0))) {
			return perform_uninstall();
		}
	}

	/* Default config */
	memset(&cfg, 0, sizeof(cfg));
	cfg.mode = MODE_PLAIN;
	cfg.table_style = TABLE_STYLE_AUTO;
	cfg.link_style = LINK_STYLE_AUTO;
	cfg.wrap_width = 80;
	cfg.crlf = 0;
	cfg.unicode_tables = 0;

	ARGBEGIN {
	case 'w':
		action = ACTION_WATCH;
		break;
	case '1':
		action = ACTION_ONCE;
		break;
	case 'k':
		action = ACTION_PASTE_ACTIVE;
		break;
	case 'p':
		action = ACTION_PASTE;
		break;
	case 'm':
		arg = EARGF(usage());
		if (strcmp(arg, "plain") == 0)
			cfg.mode = MODE_PLAIN;
		else if (strcmp(arg, "markdown") == 0 || strcmp(arg, "md") == 0)
			cfg.mode = MODE_MARKDOWN;
		else if (strcmp(arg, "slack") == 0 || strcmp(arg, "mrkdwn") == 0)
			cfg.mode = MODE_SLACK;
		else if (strcmp(arg, "jira") == 0 || strcmp(arg, "confluence") == 0)
			cfg.mode = MODE_JIRA;
		else if (strcmp(arg, "terminal") == 0 || strcmp(arg, "ansi") == 0)
			cfg.mode = MODE_TERMINAL;
		else {
			fprintf(stderr, "%s: invalid mode '%s'\n", argv0, arg);
			usage();
		}
		break;
	case 't':
		arg = EARGF(usage());
		if (strcmp(arg, "grid") == 0 || strcmp(arg, "ascii") == 0)
			cfg.table_style = TABLE_STYLE_GRID;
		else if (strcmp(arg, "markdown") == 0 || strcmp(arg, "md") == 0)
			cfg.table_style = TABLE_STYLE_MARKDOWN;
		else if (strcmp(arg, "tsv") == 0)
			cfg.table_style = TABLE_STYLE_TSV;
		else if (strcmp(arg, "simple") == 0)
			cfg.table_style = TABLE_STYLE_SIMPLE;
		else {
			fprintf(stderr, "%s: invalid table style '%s'\n", argv0, arg);
			usage();
		}
		break;
	case 'l':
		arg = EARGF(usage());
		if (strcmp(arg, "bracket") == 0)
			cfg.link_style = LINK_STYLE_BRACKET;
		else if (strcmp(arg, "inline") == 0)
			cfg.link_style = LINK_STYLE_INLINE;
		else if (strcmp(arg, "text") == 0 || strcmp(arg, "textonly") == 0)
			cfg.link_style = LINK_STYLE_TEXTONLY;
		else if (strcmp(arg, "footnote") == 0)
			cfg.link_style = LINK_STYLE_FOOTNOTE;
		else {
			fprintf(stderr, "%s: invalid link style '%s'\n", argv0, arg);
			usage();
		}
		break;
	case 'K':
		cfg.keep_tracking = 1;
		break;
	case 'u':
		cfg.unicode_tables = 1;
		break;
	case 'r':
		cfg.crlf = 1;
		break;
	case 'v':
		puts("clipbridge-" VERSION);
		return 0;
	case 'h':
	default:
		usage();
	} ARGEND

	switch (action) {
	case ACTION_ONCE:
		return clipboard_sync_once(&cfg);
	case ACTION_PASTE:
		return clipboard_paste_stdout(&cfg);
	case ACTION_PASTE_ACTIVE:
		return clipboard_paste_active(&cfg);
	case ACTION_WATCH:
	default:
		return clipboard_watch(&cfg);
	}
}
