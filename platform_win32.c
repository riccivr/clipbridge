/* See LICENSE file for copyright and license details. */
#ifdef _WIN32

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clipbridge.h"
#include "unipaste.h"
#include "i18n.h"

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_PASTE_NOW        1001
#define ID_TRAY_AUTO_FORMAT      1002
#define ID_TRAY_MODE_PLAIN       1003
#define ID_TRAY_MODE_MARKDOWN    1004
#define ID_TRAY_MODE_TERMINAL    1005
#define ID_TRAY_TABLE_GRID       1006
#define ID_TRAY_TABLE_UNICODE    1007
#define ID_TRAY_TABLE_MARKDOWN   1008
#define ID_TRAY_TABLE_TSV        1009
#define ID_TRAY_LANG_AUTO        1010
#define ID_TRAY_LANG_EN          1011
#define ID_TRAY_LANG_ES          1012
#define ID_TRAY_STARTUP          1013
#define ID_TRAY_ABOUT            1014
#define ID_TRAY_EXIT             1015

#define ID_HOTKEY_CTRL_ALT_V     2001
#define ID_HOTKEY_WIN_ALT_V      2002

static UINT cf_html = 0;
static UINT cf_ignore = 0;
static UINT cf_no_history = 0;
static UINT cf_no_cloud = 0;
static DWORD last_seq = 0;
static struct config current_cfg;
static int auto_format_default = 0; /* Off by default: Ctrl+C / Ctrl+V stay normal */
static NOTIFYICONDATAW nid;
static HWND g_hwnd = NULL;

static void
init_win32_clipboard(void)
{
	if (!cf_html) {
		cf_html = RegisterClipboardFormatA("HTML Format");
		cf_ignore = RegisterClipboardFormatA("Clipboard Viewer Ignore");
		cf_no_history = RegisterClipboardFormatA("CanIncludeInClipboardHistory");
		cf_no_cloud = RegisterClipboardFormatA("CanUploadToCloudStore");
	}
}

/* Check if clipboard content contains sensitive privacy flags (e.g. from password managers) */
static int
is_clipboard_ignored(void)
{
	init_win32_clipboard();

	if (cf_ignore && IsClipboardFormatAvailable(cf_ignore))
		return 1;

	return 0;
}

int
clipboard_read_html(char **out_html, size_t *out_len)
{
	HANDLE hData;
	char *data;
	size_t len;

	init_win32_clipboard();
	*out_html = NULL;
	*out_len = 0;

	if (is_clipboard_ignored())
		return -1;

	if (!IsClipboardFormatAvailable(cf_html))
		return -1;

	if (!OpenClipboard(NULL))
		return -1;

	hData = GetClipboardData(cf_html);
	if (!hData) {
		CloseClipboard();
		return -1;
	}

	data = (char *)GlobalLock(hData);
	if (!data) {
		CloseClipboard();
		return -1;
	}

	len = strlen(data);
	*out_html = malloc(len + 1);
	if (*out_html) {
		memcpy(*out_html, data, len + 1);
		*out_len = len;
	}

	GlobalUnlock(hData);
	CloseClipboard();

	return (*out_html) ? 0 : -1;
}

int
clipboard_write_text(const char *text, size_t len)
{
	int wlen;
	wchar_t *wstr;
	HGLOBAL hMem;
	void *pMem;

	if (!text || len == 0)
		return 0;

	/* Convert UTF-8 to UTF-16 for CF_UNICODETEXT */
	wlen = MultiByteToWideChar(CP_UTF8, 0, text, (int)len, NULL, 0);
	if (wlen <= 0)
		return -1;

	hMem = GlobalAlloc(GMEM_MOVEABLE, (wlen + 1) * sizeof(wchar_t));
	if (!hMem)
		return -1;

	pMem = GlobalLock(hMem);
	if (!pMem) {
		GlobalFree(hMem);
		return -1;
	}

	wstr = (wchar_t *)pMem;
	MultiByteToWideChar(CP_UTF8, 0, text, (int)len, wstr, wlen);
	wstr[wlen] = L'\0';
	GlobalUnlock(hMem);

	if (!OpenClipboard(NULL)) {
		GlobalFree(hMem);
		return -1;
	}

	/* Update Unicode Plaintext on Clipboard */
	SetClipboardData(CF_UNICODETEXT, hMem);
	CloseClipboard();

	return 0;
}

static int
process_html_to_clipboard(const char *html, size_t len, const struct config *cfg)
{
	struct strbuf sb;
	int ret;

	strbuf_init(&sb, len * 2);
	ret = unipaste_process_to_strbuf(html, len, &sb, cfg);
	if (ret == 0 && sb.len > 0) {
		clipboard_write_text(sb.data, sb.len);
	}
	strbuf_free(&sb);
	return ret;
}

int
clipboard_sync_once(const struct config *cfg)
{
	char *html = NULL;
	size_t len = 0;
	int ret;

	if (clipboard_read_html(&html, &len) != 0 || !html || len == 0)
		return 1;

	ret = process_html_to_clipboard(html, len, cfg);
	free(html);
	return ret;
}

int
clipboard_paste_stdout(const struct config *cfg)
{
	char *html = NULL;
	size_t len = 0;
	int ret;

	if (clipboard_read_html(&html, &len) != 0 || !html || len == 0) {
		fprintf(stderr, "clipbridge: no rich text/html found on clipboard\n");
		return 1;
	}

	ret = unipaste_process_string(html, len, stdout, cfg);
	free(html);
	return ret;
}

/* Synthesize a Ctrl+V paste keystroke to the active focused control */
static void
simulate_ctrl_v(void)
{
	INPUT inputs[4];
	memset(inputs, 0, sizeof(inputs));

	/* Ctrl key DOWN */
	inputs[0].type = INPUT_KEYBOARD;
	inputs[0].ki.wVk = VK_CONTROL;

	/* V key DOWN */
	inputs[1].type = INPUT_KEYBOARD;
	inputs[1].ki.wVk = 'V';

	/* V key UP */
	inputs[2].type = INPUT_KEYBOARD;
	inputs[2].ki.wVk = 'V';
	inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

	/* Ctrl key UP */
	inputs[3].type = INPUT_KEYBOARD;
	inputs[3].ki.wVk = VK_CONTROL;
	inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

	SendInput(4, inputs, sizeof(INPUT));
}

int
clipboard_paste_active(const struct config *cfg)
{
	char *html = NULL;
	size_t len = 0;

	if (clipboard_read_html(&html, &len) == 0 && html && len > 0) {
		struct config c = current_cfg;
		if (cfg)
			c = *cfg;
		c.crlf = 1;
		process_html_to_clipboard(html, len, &c);
		last_seq = GetClipboardSequenceNumber();
		free(html);
	}

	/* Brief delay to allow modifiers to release before simulating Ctrl+V */
	Sleep(50);
	simulate_ctrl_v();
	return 0;
}

static int
is_startup_enabled(void)
{
	HKEY hKey;
	if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
		DWORD type = 0;
		LONG res = RegQueryValueExA(hKey, "ClipBridge", NULL, &type, NULL, NULL);
		RegCloseKey(hKey);
		return (res == ERROR_SUCCESS);
	}
	return 0;
}

static void
set_startup_enabled(int enable)
{
	HKEY hKey;
	if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
		if (enable) {
			char path[MAX_PATH];
			char cmd[MAX_PATH + 16];
			GetModuleFileNameA(NULL, path, MAX_PATH);
			snprintf(cmd, sizeof(cmd), "\"%s\" -w", path);
			RegSetValueExA(hKey, "ClipBridge", 0, REG_SZ, (const BYTE *)cmd, (DWORD)strlen(cmd) + 1);
		} else {
			RegDeleteValueA(hKey, "ClipBridge");
		}
		RegCloseKey(hKey);
	}
}

static void
save_language_preference(enum lang_id lang)
{
	HKEY hKey;
	if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\ClipBridge", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
		const char *val = "auto";
		if (lang == LANG_EN) val = "en";
		else if (lang == LANG_ES) val = "es";
		RegSetValueExA(hKey, "Language", 0, REG_SZ, (const BYTE *)val, (DWORD)strlen(val) + 1);
		RegCloseKey(hKey);
	}
}

static enum lang_id
load_language_preference(void)
{
	HKEY hKey;
	if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\ClipBridge", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
		char buf[32];
		DWORD len = sizeof(buf);
		DWORD type = 0;
		if (RegQueryValueExA(hKey, "Language", NULL, &type, (LPBYTE)buf, &len) == ERROR_SUCCESS) {
			RegCloseKey(hKey);
			if (strcmp(buf, "en") == 0) return LANG_EN;
			if (strcmp(buf, "es") == 0) return LANG_ES;
			return LANG_AUTO;
		}
		RegCloseKey(hKey);
	}
	return LANG_AUTO;
}

static void
append_menu_u8(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, const char *lpNewItem)
{
	if (!lpNewItem) {
		AppendMenuW(hMenu, uFlags, uIDNewItem, NULL);
		return;
	}
	wchar_t wbuf[512];
	MultiByteToWideChar(CP_UTF8, 0, lpNewItem, -1, wbuf, sizeof(wbuf)/sizeof(wbuf[0]));
	AppendMenuW(hMenu, uFlags, uIDNewItem, wbuf);
}

static void
update_tray_tooltip(void)
{
	const char *tip = i18n_get(auto_format_default ? STR_TOOLTIP_AUTO : STR_TOOLTIP_ACTIVE);
	MultiByteToWideChar(CP_UTF8, 0, tip, -1, nid.szTip, sizeof(nid.szTip)/sizeof(nid.szTip[0]));
	Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void
show_tray_menu(HWND hwnd)
{
	POINT pt;
	HMENU hMenu = CreatePopupMenu();
	HMENU hModeMenu = CreatePopupMenu();
	HMENU hTableMenu = CreatePopupMenu();
	HMENU hLangMenu = CreatePopupMenu();

	GetCursorPos(&pt);

	/* Instant Paste Action */
	append_menu_u8(hMenu, MF_STRING, ID_TRAY_PASTE_NOW, i18n_get(STR_PASTE_ACTIVE));
	append_menu_u8(hMenu, MF_SEPARATOR, 0, NULL);

	/* Auto-format default toggle */
	append_menu_u8(hMenu, (auto_format_default ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, ID_TRAY_AUTO_FORMAT, i18n_get(STR_AUTO_FORMAT));
	append_menu_u8(hMenu, MF_SEPARATOR, 0, NULL);

	/* Mode submenu */
	append_menu_u8(hModeMenu, (current_cfg.mode == MODE_PLAIN ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, ID_TRAY_MODE_PLAIN, i18n_get(STR_MODE_PLAIN));
	append_menu_u8(hModeMenu, (current_cfg.mode == MODE_MARKDOWN ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, ID_TRAY_MODE_MARKDOWN, i18n_get(STR_MODE_MARKDOWN));
	append_menu_u8(hModeMenu, (current_cfg.mode == MODE_TERMINAL ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, ID_TRAY_MODE_TERMINAL, i18n_get(STR_MODE_TERMINAL));
	append_menu_u8(hMenu, MF_POPUP, (UINT_PTR)hModeMenu, i18n_get(STR_OUTPUT_MODE));

	/* Table submenu */
	append_menu_u8(hTableMenu, (current_cfg.table_style == TABLE_STYLE_GRID && !current_cfg.unicode_tables ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, ID_TRAY_TABLE_GRID, i18n_get(STR_TABLE_GRID));
	append_menu_u8(hTableMenu, (current_cfg.unicode_tables ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, ID_TRAY_TABLE_UNICODE, i18n_get(STR_TABLE_UNICODE));
	append_menu_u8(hTableMenu, (current_cfg.table_style == TABLE_STYLE_MARKDOWN ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, ID_TRAY_TABLE_MARKDOWN, i18n_get(STR_TABLE_MARKDOWN));
	append_menu_u8(hTableMenu, (current_cfg.table_style == TABLE_STYLE_TSV ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, ID_TRAY_TABLE_TSV, i18n_get(STR_TABLE_TSV));
	append_menu_u8(hMenu, MF_POPUP, (UINT_PTR)hTableMenu, i18n_get(STR_TABLE_STYLE));

	/* Language submenu */
	append_menu_u8(hLangMenu, (i18n_get_language() == LANG_AUTO ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, ID_TRAY_LANG_AUTO, i18n_get(STR_LANG_AUTO));
	append_menu_u8(hLangMenu, (i18n_get_language() == LANG_EN ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, ID_TRAY_LANG_EN, i18n_get(STR_LANG_EN));
	append_menu_u8(hLangMenu, (i18n_get_language() == LANG_ES ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, ID_TRAY_LANG_ES, i18n_get(STR_LANG_ES));
	append_menu_u8(hMenu, MF_POPUP, (UINT_PTR)hLangMenu, i18n_get(STR_LANGUAGE));

	append_menu_u8(hMenu, MF_SEPARATOR, 0, NULL);
	append_menu_u8(hMenu, (is_startup_enabled() ? MF_CHECKED : MF_UNCHECKED) | MF_STRING, ID_TRAY_STARTUP, i18n_get(STR_STARTUP));
	append_menu_u8(hMenu, MF_STRING, ID_TRAY_ABOUT, i18n_get(STR_ABOUT_MENU));
	append_menu_u8(hMenu, MF_SEPARATOR, 0, NULL);
	append_menu_u8(hMenu, MF_STRING, ID_TRAY_EXIT, i18n_get(STR_EXIT));

	SetForegroundWindow(hwnd);
	TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
	PostMessage(hwnd, WM_NULL, 0, 0);

	DestroyMenu(hLangMenu);
	DestroyMenu(hTableMenu);
	DestroyMenu(hModeMenu);
	DestroyMenu(hMenu);
}

static LRESULT CALLBACK
WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CLIPBOARDUPDATE: {
		if (!auto_format_default)
			return 0;

		DWORD seq = GetClipboardSequenceNumber();
		if (seq != last_seq) {
			last_seq = seq;
			char *html = NULL;
			size_t len = 0;
			if (clipboard_read_html(&html, &len) == 0 && html && len > 0) {
				struct config cfg = current_cfg;
				/* Ensure Windows CRLF on Win32 by default */
				cfg.crlf = 1;
				process_html_to_clipboard(html, len, &cfg);
				last_seq = GetClipboardSequenceNumber();
				free(html);
			}
		}
		return 0;
	}

	case WM_HOTKEY: {
		if (wParam == ID_HOTKEY_CTRL_ALT_V || wParam == ID_HOTKEY_WIN_ALT_V) {
			clipboard_paste_active(&current_cfg);
		}
		return 0;
	}

	case WM_TRAYICON: {
		if (lParam == WM_RBUTTONUP) {
			show_tray_menu(hwnd);
		} else if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP) {
			clipboard_paste_active(&current_cfg);
		}
		return 0;
	}

	case WM_COMMAND: {
		int wmId = LOWORD(wParam);
		switch (wmId) {
		case ID_TRAY_PASTE_NOW:
			clipboard_paste_active(&current_cfg);
			break;
		case ID_TRAY_AUTO_FORMAT:
			auto_format_default = !auto_format_default;
			update_tray_tooltip();
			break;
		case ID_TRAY_MODE_PLAIN:
			current_cfg.mode = MODE_PLAIN;
			break;
		case ID_TRAY_MODE_MARKDOWN:
			current_cfg.mode = MODE_MARKDOWN;
			break;
		case ID_TRAY_MODE_TERMINAL:
			current_cfg.mode = MODE_TERMINAL;
			break;
		case ID_TRAY_TABLE_GRID:
			current_cfg.table_style = TABLE_STYLE_GRID;
			current_cfg.unicode_tables = 0;
			break;
		case ID_TRAY_TABLE_UNICODE:
			current_cfg.table_style = TABLE_STYLE_GRID;
			current_cfg.unicode_tables = 1;
			break;
		case ID_TRAY_TABLE_MARKDOWN:
			current_cfg.table_style = TABLE_STYLE_MARKDOWN;
			break;
		case ID_TRAY_TABLE_TSV:
			current_cfg.table_style = TABLE_STYLE_TSV;
			break;
		case ID_TRAY_LANG_AUTO:
			i18n_set_language(LANG_AUTO);
			save_language_preference(LANG_AUTO);
			update_tray_tooltip();
			break;
		case ID_TRAY_LANG_EN:
			i18n_set_language(LANG_EN);
			save_language_preference(LANG_EN);
			update_tray_tooltip();
			break;
		case ID_TRAY_LANG_ES:
			i18n_set_language(LANG_ES);
			save_language_preference(LANG_ES);
			update_tray_tooltip();
			break;
		case ID_TRAY_STARTUP:
			set_startup_enabled(!is_startup_enabled());
			break;
		case ID_TRAY_ABOUT: {
			wchar_t title[128];
			wchar_t body[1024];
			MultiByteToWideChar(CP_UTF8, 0, i18n_get(STR_ABOUT_TITLE), -1, title, sizeof(title)/sizeof(title[0]));
			MultiByteToWideChar(CP_UTF8, 0, i18n_get(STR_ABOUT_BODY), -1, body, sizeof(body)/sizeof(body[0]));
			MessageBoxW(hwnd, body, title, MB_OK | MB_ICONINFORMATION);
			break;
		}
		case ID_TRAY_EXIT:
			PostQuitMessage(0);
			break;
		}
		return 0;
	}

	case WM_DESTROY:
		UnregisterHotKey(hwnd, ID_HOTKEY_CTRL_ALT_V);
		UnregisterHotKey(hwnd, ID_HOTKEY_WIN_ALT_V);
		Shell_NotifyIconW(NIM_DELETE, &nid);
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

int
clipboard_watch(const struct config *cfg)
{
	WNDCLASSA wc;
	MSG msg;

	if (cfg) {
		current_cfg = *cfg;
	} else {
		memset(&current_cfg, 0, sizeof(current_cfg));
		current_cfg.mode = MODE_PLAIN;
	}
	current_cfg.crlf = 1;

	/* Initialize i18n subsystem with saved user preference */
	i18n_init(load_language_preference());

	/* Singleton Enforcement: Allow only 1 running daemon instance */
	HANDLE hMutex = CreateMutexA(NULL, TRUE, "Local\\ClipBridge_SingleInstance_Mutex_Ricci");
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		HWND existing = FindWindowA("ClipBridgeMonitorClass", "ClipBridge");
		if (existing) {
			PostMessageA(existing, WM_COMMAND, ID_TRAY_ABOUT, 0);
		}
		if (hMutex)
			CloseHandle(hMutex);
		return 0;
	}

	init_win32_clipboard();

	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = WndProc;
	wc.hInstance = GetModuleHandle(NULL);
	wc.lpszClassName = "ClipBridgeMonitorClass";

	RegisterClassA(&wc);
	g_hwnd = CreateWindowExA(0, wc.lpszClassName, "ClipBridge", 0, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
	if (!g_hwnd) {
		return 1;
	}

	if (!AddClipboardFormatListener(g_hwnd)) {
		DestroyWindow(g_hwnd);
		return 1;
	}

	/* Register global hotkeys: Ctrl+Alt+V and Win+Alt+V */
	RegisterHotKey(g_hwnd, ID_HOTKEY_CTRL_ALT_V, MOD_CONTROL | MOD_ALT, 'V');
	RegisterHotKey(g_hwnd, ID_HOTKEY_WIN_ALT_V, MOD_WIN | MOD_ALT, 'V');

	/* Initialize and display system tray notification icon */
	memset(&nid, 0, sizeof(nid));
	nid.cbSize = sizeof(NOTIFYICONDATAW);
	nid.hWnd = g_hwnd;
	nid.uID = 1;
	nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	nid.uCallbackMessage = WM_TRAYICON;
	nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(1));
	if (!nid.hIcon)
		nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	const char *tip = i18n_get(STR_TOOLTIP_ACTIVE);
	MultiByteToWideChar(CP_UTF8, 0, tip, -1, nid.szTip, sizeof(nid.szTip)/sizeof(nid.szTip[0]));

	Shell_NotifyIconW(NIM_ADD, &nid);

	while (GetMessage(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	RemoveClipboardFormatListener(g_hwnd);
	UnregisterHotKey(g_hwnd, ID_HOTKEY_CTRL_ALT_V);
	UnregisterHotKey(g_hwnd, ID_HOTKEY_WIN_ALT_V);
	Shell_NotifyIconW(NIM_DELETE, &nid);
	DestroyWindow(g_hwnd);
	return 0;
}

#else
typedef int iso_c_dummy_clipbridge_win32;
#endif /* _WIN32 */
