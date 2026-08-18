/* See LICENSE file for copyright and license details. */
#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clipbridge.h"

static UINT cf_html = 0;
static DWORD last_seq = 0;

static void
init_win32_clipboard(void)
{
	if (!cf_html) {
		cf_html = RegisterClipboardFormatA("HTML Format");
	}
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
	ret = unipaste_process_string(html, len, NULL, cfg);
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

static LRESULT CALLBACK
WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_CLIPBOARDUPDATE) {
		DWORD seq = GetClipboardSequenceNumber();
		if (seq != last_seq) {
			last_seq = seq;
			char *html = NULL;
			size_t len = 0;
			if (clipboard_read_html(&html, &len) == 0 && html && len > 0) {
				struct config cfg;
				memset(&cfg, 0, sizeof(cfg));
				cfg.mode = MODE_PLAIN;
				cfg.crlf = 1; /* Windows Notepad loves CRLF */
				process_html_to_clipboard(html, len, &cfg);
				last_seq = GetClipboardSequenceNumber();
				free(html);
			}
		}
		return 0;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

int
clipboard_watch(const struct config *cfg)
{
	WNDCLASSA wc;
	HWND hwnd;
	MSG msg;

	(void)cfg;
	init_win32_clipboard();

	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = WndProc;
	wc.hInstance = GetModuleHandle(NULL);
	wc.lpszClassName = "ClipBridgeMonitorClass";

	RegisterClassA(&wc);
	hwnd = CreateWindowExA(0, wc.lpszClassName, "ClipBridgeMonitor", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
	if (!hwnd) {
		fprintf(stderr, "clipbridge: failed to create message window\n");
		return 1;
	}

	if (!AddClipboardFormatListener(hwnd)) {
		fprintf(stderr, "clipbridge: failed to add clipboard format listener\n");
		DestroyWindow(hwnd);
		return 1;
	}

	printf("clipbridge: monitoring Windows clipboard in background (Ctrl+C to stop)...\n");
	fflush(stdout);

	while (GetMessage(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	RemoveClipboardFormatListener(hwnd);
	DestroyWindow(hwnd);
	return 0;
}

#else
typedef int iso_c_dummy_clipbridge_win32;
#endif /* _WIN32 */
