/* See LICENSE file for copyright and license details. */
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <winhttp.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "i18n.h"
#include "update_win32.h"

#ifndef VERSION
#define VERSION "0.0.0"
#endif

#define UPDATE_API_HOST   L"api.github.com"
#define UPDATE_API_PATH   L"/repos/riccivr/clipbridge/releases/latest"
#define UPDATE_UA         L"ClipBridge/" VERSION
#define SETTINGS_KEY      "Software\\ClipBridge\\Settings"
#define UNINSTALL_KEY     "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ClipBridge"
#define DAY_SECS          86400

struct release_info {
	char tag[64];
	char download_url[1024];
	int is_zip;
};


static int
utf8_to_wide(const char *src, wchar_t *dst, int dst_chars)
{
	if (!src || !dst || dst_chars <= 0)
		return 0;
	return MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, dst_chars);
}


static void
msg_u8(HWND hwnd, enum str_id body_id, enum str_id title_id, UINT flags)
{
	wchar_t title[256], body[1024];
	utf8_to_wide(i18n_get(title_id), title, 256);
	utf8_to_wide(i18n_get(body_id), body, 1024);
	MessageBoxW(hwnd, body, title, flags);
}

static int
json_unescape_copy(const char *src, size_t n, char *dst, size_t dstsz)
{
	size_t i = 0, j = 0;
	if (!dst || dstsz == 0)
		return -1;
	while (i < n && j + 1 < dstsz) {
		if (src[i] == '\\' && i + 1 < n) {
			char c = src[i + 1];
			if (c == '"' || c == '\\' || c == '/') {
				dst[j++] = c;
				i += 2;
				continue;
			}
			if (c == 'n') { dst[j++] = '\n'; i += 2; continue; }
			if (c == 't') { dst[j++] = '\t'; i += 2; continue; }
		}
		dst[j++] = src[i++];
	}
	dst[j] = '\0';
	return 0;
}

/* Extract JSON string value for "key" starting search at *pos (updated). */
static int
json_next_string(const char *json, size_t *pos, const char *key, char *out, size_t outsz)
{
	char pattern[128];
	const char *p, *start, *end;
	size_t klen, vlen;

	if (!json || !pos || !key || !out || outsz == 0)
		return -1;

	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	klen = strlen(pattern);
	p = strstr(json + *pos, pattern);
	if (!p)
		return -1;
	p += klen;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	if (*p != ':')
		return -1;
	p++;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	if (*p != '"')
		return -1;
	start = ++p;
	end = start;
	while (*end && *end != '"') {
		if (*end == '\\' && end[1])
			end += 2;
		else
			end++;
	}
	if (*end != '"')
		return -1;
	vlen = (size_t)(end - start);
	if (json_unescape_copy(start, vlen, out, outsz) != 0)
		return -1;
	*pos = (size_t)(end - json) + 1;
	return 0;
}

static int
json_find_string(const char *json, const char *key, char *out, size_t outsz)
{
	size_t pos = 0;
	return json_next_string(json, &pos, key, out, outsz);
}

static int
version_strip_v(const char *in, char *out, size_t outsz)
{
	if (!in || !out || outsz == 0)
		return -1;
	if (in[0] == 'v' || in[0] == 'V')
		in++;
	snprintf(out, outsz, "%s", in);
	return 0;
}

/* Compare dotted numeric versions; >0 if a newer than b. */
static int
version_cmp(const char *a, const char *b)
{
	char aa[64], bb[64];
	const char *pa, *pb;

	version_strip_v(a, aa, sizeof(aa));
	version_strip_v(b, bb, sizeof(bb));
	pa = aa;
	pb = bb;
	while (*pa || *pb) {
		long va = 0, vb = 0;
		if (*pa) {
			va = strtol(pa, (char **)&pa, 10);
			if (*pa == '.')
				pa++;
		}
		if (*pb) {
			vb = strtol(pb, (char **)&pb, 10);
			if (*pb == '.')
				pb++;
		}
		if (va != vb)
			return (va > vb) ? 1 : -1;
	}
	return 0;
}

int
update_auto_enabled(void)
{
	HKEY hKey;
	DWORD val = 1, sz = sizeof(val), type = 0;

	if (RegOpenKeyExA(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
		if (RegQueryValueExA(hKey, "AutoUpdate", NULL, &type, (LPBYTE)&val, &sz) != ERROR_SUCCESS)
			val = 1;
		RegCloseKey(hKey);
	}
	return val ? 1 : 0;
}

void
update_set_auto_enabled(int enable)
{
	HKEY hKey;
	DWORD val = enable ? 1 : 0;

	if (RegCreateKeyExA(HKEY_CURRENT_USER, SETTINGS_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
		RegSetValueExA(hKey, "AutoUpdate", 0, REG_DWORD, (const BYTE *)&val, sizeof(val));
		RegCloseKey(hKey);
	}
}

static void
save_last_check(void)
{
	HKEY hKey;
	DWORD now = (DWORD)time(NULL);

	if (RegCreateKeyExA(HKEY_CURRENT_USER, SETTINGS_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
		RegSetValueExA(hKey, "LastUpdateCheck", 0, REG_DWORD, (const BYTE *)&now, sizeof(now));
		RegCloseKey(hKey);
	}
}

static int
should_auto_check(void)
{
	HKEY hKey;
	DWORD last = 0, sz = sizeof(last), type = 0;
	DWORD now = (DWORD)time(NULL);

	if (!update_auto_enabled())
		return 0;
	if (RegOpenKeyExA(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
		if (RegQueryValueExA(hKey, "LastUpdateCheck", NULL, &type, (LPBYTE)&last, &sz) == ERROR_SUCCESS) {
			RegCloseKey(hKey);
			if (now >= last && (now - last) < DAY_SECS)
				return 0;
			return 1;
		}
		RegCloseKey(hKey);
	}
	return 1;
}

static char *
http_get_utf8(const wchar_t *host, INTERNET_PORT port, const wchar_t *path, int secure, DWORD *out_len)
{
	HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
	DWORD status = 0, status_sz = sizeof(status), avail, read;
	char *buf = NULL;
	size_t cap = 0, len = 0;
	BOOL ok;
	DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;

	if (out_len)
		*out_len = 0;

	hSession = WinHttpOpen(UPDATE_UA, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession)
		goto fail;

	hConnect = WinHttpConnect(hSession, host, port, 0);
	if (!hConnect)
		goto fail;

	hRequest = WinHttpOpenRequest(hConnect, L"GET", path, NULL,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
	if (!hRequest)
		goto fail;

	WinHttpAddRequestHeaders(hRequest,
		L"Accept: application/vnd.github+json\r\n"
		L"X-GitHub-Api-Version: 2022-11-28",
		(ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);

	ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
	if (!ok)
		goto fail;
	ok = WinHttpReceiveResponse(hRequest, NULL);
	if (!ok)
		goto fail;

	if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_sz, WINHTTP_NO_HEADER_INDEX))
		goto fail;
	if (status < 200 || status >= 300)
		goto fail;

	for (;;) {
		avail = 0;
		if (!WinHttpQueryDataAvailable(hRequest, &avail))
			goto fail;
		if (avail == 0)
			break;
		if (len + avail + 1 > cap) {
			size_t ncap = (cap ? cap * 2 : 65536);
			char *nbuf;
			while (ncap < len + avail + 1)
				ncap *= 2;
			nbuf = realloc(buf, ncap);
			if (!nbuf)
				goto fail;
			buf = nbuf;
			cap = ncap;
		}
		if (!WinHttpReadData(hRequest, buf + len, avail, &read))
			goto fail;
		len += read;
	}
	if (!buf) {
		buf = malloc(1);
		if (!buf)
			goto fail;
		len = 0;
	}
	buf[len] = '\0';
	if (out_len)
		*out_len = (DWORD)len;

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return buf;

fail:
	free(buf);
	if (hRequest) WinHttpCloseHandle(hRequest);
	if (hConnect) WinHttpCloseHandle(hConnect);
	if (hSession) WinHttpCloseHandle(hSession);
	return NULL;
}

static int
parse_url(const char *url, wchar_t *host, size_t host_n, wchar_t *path, size_t path_n, INTERNET_PORT *port, int *secure)
{
	URL_COMPONENTSW uc;
	wchar_t wurl[2048];
	wchar_t whost[256];
	wchar_t wpath[1536];
	wchar_t wextra[512];

	if (!utf8_to_wide(url, wurl, 2048))
		return -1;

	memset(&uc, 0, sizeof(uc));
	uc.dwStructSize = sizeof(uc);
	uc.lpszHostName = whost;
	uc.dwHostNameLength = (DWORD)(sizeof(whost) / sizeof(whost[0]));
	uc.lpszUrlPath = wpath;
	uc.dwUrlPathLength = (DWORD)(sizeof(wpath) / sizeof(wpath[0]));
	uc.lpszExtraInfo = wextra;
	uc.dwExtraInfoLength = (DWORD)(sizeof(wextra) / sizeof(wextra[0]));

	if (!WinHttpCrackUrl(wurl, 0, 0, &uc))
		return -1;

	wcsncpy(host, whost, host_n - 1);
	host[host_n - 1] = L'\0';
	if (wextra[0])
		_snwprintf(path, path_n, L"%ls%ls", wpath, wextra);
	else {
		wcsncpy(path, wpath, path_n - 1);
		path[path_n - 1] = L'\0';
	}
	*port = uc.nPort;
	*secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
	return 0;
}

static void
pump_messages(HWND hwnd)
{
	MSG msg;
	while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			PostQuitMessage((int)msg.wParam);
			break;
		}
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	(void)hwnd;
}

static LRESULT CALLBACK
ProgressWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	(void)wParam;
	(void)lParam;
	if (msg == WM_CLOSE)
		return 0;
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static HWND
create_progress_window(HWND owner, HWND *out_bar)
{
	static int registered = 0;
	WNDCLASSW wc;
	HWND hwnd, bar;
	RECT rc;
	int w = 360, h = 80;
	wchar_t title[128];
	INITCOMMONCONTROLSEX icex;

	icex.dwSize = sizeof(icex);
	icex.dwICC = ICC_PROGRESS_CLASS;
	InitCommonControlsEx(&icex);

	if (!registered) {
		memset(&wc, 0, sizeof(wc));
		wc.lpfnWndProc = ProgressWndProc;
		wc.hInstance = GetModuleHandleW(NULL);
		wc.lpszClassName = L"ClipBridgeUpdateProgress";
		wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		RegisterClassW(&wc);
		registered = 1;
	}

	utf8_to_wide(i18n_get(STR_UPDATE_PROGRESS), title, 128);
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0);
	hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
		L"ClipBridgeUpdateProgress", title,
		WS_CAPTION | WS_POPUP | WS_VISIBLE,
		rc.left + ((rc.right - rc.left) - w) / 2,
		rc.top + ((rc.bottom - rc.top) - h) / 2,
		w, h, owner, NULL, GetModuleHandleW(NULL), NULL);
	if (!hwnd)
		return NULL;

	bar = CreateWindowExW(0, PROGRESS_CLASSW, NULL,
		WS_CHILD | WS_VISIBLE,
		12, 24, w - 40, 20, hwnd, NULL, GetModuleHandleW(NULL), NULL);
	if (bar) {
		SendMessageW(bar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
		SendMessageW(bar, PBM_SETPOS, 0, 0);
	}
	*out_bar = bar;
	UpdateWindow(hwnd);
	return hwnd;
}

static int
http_download_file(const char *url, const wchar_t *dest_path, HWND owner)
{
	wchar_t host[256], path[1536];
	INTERNET_PORT port = 0;
	int secure = 0;
	HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
	DWORD status = 0, status_sz = sizeof(status), avail, read, written;
	DWORD content_len = 0, cl_sz = sizeof(content_len);
	HANDLE hFile = INVALID_HANDLE_VALUE;
	HWND hwnd_prog = NULL, hwnd_bar = NULL;
	BYTE chunk[8192];
	DWORD got = 0;
	BOOL ok;
	DWORD flags;

	if (parse_url(url, host, 256, path, 1536, &port, &secure) != 0)
		return -1;

	hwnd_prog = create_progress_window(owner, &hwnd_bar);

	hSession = WinHttpOpen(UPDATE_UA, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession)
		goto fail;

	hConnect = WinHttpConnect(hSession, host, port, 0);
	if (!hConnect)
		goto fail;

	flags = secure ? WINHTTP_FLAG_SECURE : 0;
	hRequest = WinHttpOpenRequest(hConnect, L"GET", path, NULL,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
	if (!hRequest)
		goto fail;

	ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
	if (!ok)
		goto fail;
	ok = WinHttpReceiveResponse(hRequest, NULL);
	if (!ok)
		goto fail;

	if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_sz, WINHTTP_NO_HEADER_INDEX))
		goto fail;
	if (status < 200 || status >= 300)
		goto fail;

	WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &content_len, &cl_sz, WINHTTP_NO_HEADER_INDEX);

	hFile = CreateFileW(dest_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		goto fail;

	for (;;) {
		avail = 0;
		if (!WinHttpQueryDataAvailable(hRequest, &avail))
			goto fail;
		if (avail == 0)
			break;
		while (avail > 0) {
			DWORD to_read = avail > sizeof(chunk) ? sizeof(chunk) : avail;
			if (!WinHttpReadData(hRequest, chunk, to_read, &read) || read == 0)
				goto fail;
			if (!WriteFile(hFile, chunk, read, &written, NULL) || written != read)
				goto fail;
			got += read;
			avail -= read;
			if (hwnd_bar && content_len > 0) {
				int pct = (int)((got * 100ull) / content_len);
				if (pct > 100) pct = 100;
				SendMessageW(hwnd_bar, PBM_SETPOS, pct, 0);
			} else if (hwnd_bar) {
				SendMessageW(hwnd_bar, PBM_SETPOS, (got / 8192) % 100, 0);
			}
			pump_messages(owner);
		}
	}

	CloseHandle(hFile);
	hFile = INVALID_HANDLE_VALUE;
	if (hwnd_prog)
		DestroyWindow(hwnd_prog);
	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return 0;

fail:
	if (hFile != INVALID_HANDLE_VALUE) {
		CloseHandle(hFile);
		DeleteFileW(dest_path);
	}
	if (hwnd_prog)
		DestroyWindow(hwnd_prog);
	if (hRequest) WinHttpCloseHandle(hRequest);
	if (hConnect) WinHttpCloseHandle(hConnect);
	if (hSession) WinHttpCloseHandle(hSession);
	return -1;
}

static int
asset_rank(const char *name)
{
	if (!name)
		return 100;
	if (_stricmp(name, "clipbridge-portable.exe") == 0)
		return 0;
	if (_stricmp(name, "clipbridge-portable-x64.exe") == 0)
		return 1;
	if (_stricmp(name, "clipbridge.exe") == 0)
		return 2;
	{
		size_t n = strlen(name);
		if (n > 4 && _stricmp(name + n - 4, ".zip") == 0) {
			if (strstr(name, "windows") || strstr(name, "win") || strstr(name, "x64"))
				return 10;
			return 20;
		}
	}
	return 100;
}

static int
parse_release(const char *json, struct release_info *info)
{
	size_t pos = 0;
	char name[256], url[1024];
	int best = 100;

	memset(info, 0, sizeof(*info));
	if (json_find_string(json, "tag_name", info->tag, sizeof(info->tag)) != 0)
		return -1;

	/* Walk assets: each object has "name" then "browser_download_url" */
	while (json_next_string(json, &pos, "name", name, sizeof(name)) == 0) {
		size_t urlpos = pos;
		int rank = asset_rank(name);
		if (rank >= 100)
			continue;
		if (json_next_string(json, &urlpos, "browser_download_url", url, sizeof(url)) != 0)
			continue;
		/* Ensure URL is still inside this asset (before next name far away is ok;
		   require url appears before the next "name" roughly — simple: accept). */
		if (rank < best) {
			best = rank;
			snprintf(info->download_url, sizeof(info->download_url), "%s", url);
			info->is_zip = (rank >= 10) ? 1 : 0;
			if (best == 0)
				break;
		}
		pos = urlpos;
	}
	if (best >= 100 || info->download_url[0] == '\0')
		return -1;
	return 0;
}

static int
path_under_localappdata_clipbridge(const wchar_t *exe_path)
{
	wchar_t local[MAX_PATH];
	wchar_t prefix[MAX_PATH];
	size_t n;

	if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local)))
		return 0;
	_snwprintf(prefix, MAX_PATH, L"%ls\\ClipBridge", local);
	n = wcslen(prefix);
	if (_wcsnicmp(exe_path, prefix, n) != 0)
		return 0;
	return (exe_path[n] == L'\\' || exe_path[n] == L'\0');
}

static void
bump_uninstall_version(const char *tag)
{
	HKEY hKey;
	char ver[64];
	wchar_t wver[64];

	version_strip_v(tag, ver, sizeof(ver));
	utf8_to_wide(ver, wver, 64);
	if (RegOpenKeyExA(HKEY_CURRENT_USER, UNINSTALL_KEY, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
		RegSetValueExW(hKey, L"DisplayVersion", 0, REG_SZ,
			(const BYTE *)wver, (DWORD)((wcslen(wver) + 1) * sizeof(wchar_t)));
		RegCloseKey(hKey);
	}
}

static int
extract_exe_from_zip(const wchar_t *zip_path, const wchar_t *dest_exe)
{
	wchar_t tmpdir[MAX_PATH];
	wchar_t cmd[MAX_PATH * 4];
	wchar_t candidate[MAX_PATH];
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	DWORD code = 1;
	const wchar_t *names[] = {
		L"clipbridge-portable.exe",
		L"clipbridge.exe",
		NULL
	};
	int i;

	_snwprintf(tmpdir, MAX_PATH, L"%ls.dir", zip_path);
	CreateDirectoryW(tmpdir, NULL);

	/* Windows 10+ tar extracts zip archives */
	_snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]),
		L"tar -xf \"%ls\" -C \"%ls\"", zip_path, tmpdir);

	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	memset(&pi, 0, sizeof(pi));

	if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
		goto fail;
	WaitForSingleObject(pi.hProcess, 60000);
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	if (code != 0)
		goto fail;

	for (i = 0; names[i]; i++) {
		_snwprintf(candidate, MAX_PATH, L"%ls\\%ls", tmpdir, names[i]);
		if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES) {
			if (!CopyFileW(candidate, dest_exe, FALSE))
				goto fail;
			/* best-effort cleanup */
			DeleteFileW(candidate);
			RemoveDirectoryW(tmpdir);
			DeleteFileW(zip_path);
			return 0;
		}
	}

fail:
	RemoveDirectoryW(tmpdir);
	return -1;
}

static int
apply_update(HWND hwnd, NOTIFYICONDATAW *nid, const struct release_info *info)
{
	wchar_t exe_path[MAX_PATH];
	wchar_t new_path[MAX_PATH];
	wchar_t zip_path[MAX_PATH];
	wchar_t dir[MAX_PATH];
	wchar_t cmd[MAX_PATH * 4];
	wchar_t *slash;
	char msg[512];
	wchar_t wmsg[512], wtitle[128];

	if (!GetModuleFileNameW(NULL, exe_path, MAX_PATH))
		return -1;

	wcsncpy(dir, exe_path, MAX_PATH - 1);
	dir[MAX_PATH - 1] = L'\0';
	slash = wcsrchr(dir, L'\\');
	if (slash)
		*slash = L'\0';
	else
		dir[0] = L'\0';

	_snwprintf(new_path, MAX_PATH, L"%ls.new", exe_path);

	snprintf(msg, sizeof(msg), i18n_get(STR_UPDATE_AVAILABLE), info->tag);
	utf8_to_wide(msg, wmsg, 512);
	utf8_to_wide(i18n_get(STR_UPDATE_TITLE), wtitle, 128);
	if (MessageBoxW(hwnd, wmsg, wtitle, MB_YESNO | MB_ICONQUESTION) != IDYES)
		return 0;

	if (info->is_zip) {
		_snwprintf(zip_path, MAX_PATH, L"%ls\\clipbridge-update.zip", dir);
		if (http_download_file(info->download_url, zip_path, hwnd) != 0) {
			if (hwnd)
				msg_u8(hwnd, STR_UPDATE_FAILED, STR_UPDATE_TITLE, MB_OK | MB_ICONERROR);
			return -1;
		}
		DeleteFileW(new_path);
		if (extract_exe_from_zip(zip_path, new_path) != 0) {
			DeleteFileW(zip_path);
			if (hwnd)
				msg_u8(hwnd, STR_UPDATE_FAILED, STR_UPDATE_TITLE, MB_OK | MB_ICONERROR);
			return -1;
		}
	} else {
		if (http_download_file(info->download_url, new_path, hwnd) != 0) {
			if (hwnd)
				msg_u8(hwnd, STR_UPDATE_FAILED, STR_UPDATE_TITLE, MB_OK | MB_ICONERROR);
			return -1;
		}
	}

	if (path_under_localappdata_clipbridge(exe_path))
		bump_uninstall_version(info->tag);

	if (nid)
		Shell_NotifyIconW(NIM_DELETE, nid);

	/* Delayed replace: wait briefly, move .new over live exe, relaunch -w */
	_snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]),
		L"/C ping 127.0.0.1 -n 2 > nul & move /Y \"%ls\" \"%ls\" & start \"\" \"%ls\" -w",
		new_path, exe_path, exe_path);
	ShellExecuteW(NULL, L"open", L"cmd.exe", cmd, dir[0] ? dir : NULL, SW_HIDE);
	PostQuitMessage(0);
	return 1;
}

void
update_check(HWND hwnd, NOTIFYICONDATAW *nid, int quiet)
{
	char *json;
	DWORD len = 0;
	struct release_info info;
	char cur[64];

	json = http_get_utf8(UPDATE_API_HOST, INTERNET_DEFAULT_HTTPS_PORT, UPDATE_API_PATH, 1, &len);
	save_last_check();
	if (!json) {
		if (!quiet)
			msg_u8(hwnd, STR_UPDATE_FAILED, STR_UPDATE_TITLE, MB_OK | MB_ICONWARNING);
		return;
	}

	if (parse_release(json, &info) != 0) {
		free(json);
		if (!quiet)
			msg_u8(hwnd, STR_UPDATE_FAILED, STR_UPDATE_TITLE, MB_OK | MB_ICONWARNING);
		return;
	}
	free(json);

	snprintf(cur, sizeof(cur), "v%s", VERSION);
	if (version_cmp(info.tag, cur) <= 0) {
		if (!quiet)
			msg_u8(hwnd, STR_UPDATE_NONE, STR_UPDATE_TITLE, MB_OK | MB_ICONINFORMATION);
		return;
	}

	apply_update(hwnd, nid, &info);
}

void
update_maybe_auto_check(HWND hwnd, NOTIFYICONDATAW *nid)
{
	if (!should_auto_check())
		return;
	update_check(hwnd, nid, 1);
}

#else
typedef int iso_c_dummy_clipbridge_update_win32;
#endif /* _WIN32 */
