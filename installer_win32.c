/* See LICENSE file for copyright and license details. */
#ifdef _WIN32

#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "i18n.h"

#define VERSION "1.1.0"

static enum lang_id selected_lang = LANG_EN;
static int user_cancelled = 0;
static HWND hwndCombo = NULL;

static int
create_shortcut(const char *target_path, const char *shortcut_path, const char *description, const char *icon_path)
{
	IShellLinkA *pShellLink = NULL;
	IPersistFile *pPersistFile = NULL;
	HRESULT hr;
	WCHAR wsz[MAX_PATH];

	CoInitialize(NULL);
	hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkA, (void **)&pShellLink);
	if (SUCCEEDED(hr)) {
		pShellLink->lpVtbl->SetPath(pShellLink, target_path);
		pShellLink->lpVtbl->SetDescription(pShellLink, description);
		if (icon_path) {
			pShellLink->lpVtbl->SetIconLocation(pShellLink, icon_path, 0);
		}

		hr = pShellLink->lpVtbl->QueryInterface(pShellLink, &IID_IPersistFile, (void **)&pPersistFile);
		if (SUCCEEDED(hr)) {
			MultiByteToWideChar(CP_ACP, 0, shortcut_path, -1, wsz, MAX_PATH);
			hr = pPersistFile->lpVtbl->Save(pPersistFile, wsz, TRUE);
			pPersistFile->lpVtbl->Release(pPersistFile);
		}
		pShellLink->lpVtbl->Release(pShellLink);
	}
	CoUninitialize();
	return SUCCEEDED(hr);
}

static LRESULT CALLBACK
LangDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE: {
		/* Label */
		CreateWindowA("STATIC", "Please select your language / Por favor seleccione su idioma:",
			WS_VISIBLE | WS_CHILD | SS_LEFT, 25, 20, 360, 20, hwnd, NULL, NULL, NULL);

		/* ComboBox with languages */
		hwndCombo = CreateWindowA("COMBOBOX", "",
			WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
			25, 48, 340, 120, hwnd, (HMENU)101, NULL, NULL);

		SendMessageA(hwndCombo, CB_ADDSTRING, 0, (LPARAM)"English");
		SendMessageA(hwndCombo, CB_ADDSTRING, 0, (LPARAM)"Español (Spanish)");

		/* Pre-select based on system UI language */
		enum lang_id sys_lang = (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_SPANISH) ? LANG_ES : LANG_EN;
		SendMessageA(hwndCombo, CB_SETCURSEL, (sys_lang == LANG_ES ? 1 : 0), 0);

		/* OK Button */
		CreateWindowA("BUTTON", "OK / Aceptar",
			WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | WS_TABSTOP,
			160, 95, 100, 30, hwnd, (HMENU)IDOK, NULL, NULL);

		/* Cancel Button */
		CreateWindowA("BUTTON", "Cancel / Cancelar",
			WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
			270, 95, 95, 30, hwnd, (HMENU)IDCANCEL, NULL, NULL);
		return 0;
	}
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK) {
			int cur = (int)SendMessageA(hwndCombo, CB_GETCURSEL, 0, 0);
			selected_lang = (cur == 1) ? LANG_ES : LANG_EN;
			DestroyWindow(hwnd);
		} else if (LOWORD(wParam) == IDCANCEL) {
			user_cancelled = 1;
			DestroyWindow(hwnd);
		}
		return 0;
	case WM_CLOSE:
		user_cancelled = 1;
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static enum lang_id
prompt_user_language(HINSTANCE hInstance)
{
	WNDCLASSA wc;
	HWND hwnd;
	MSG msg;

	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = LangDialogProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = "ClipBridgeLangPickerClass";
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));

	RegisterClassA(&wc);

	/* Center dialog window on screen */
	int screenW = GetSystemMetrics(SM_CXSCREEN);
	int screenH = GetSystemMetrics(SM_CYSCREEN);
	int dlgW = 405;
	int dlgH = 180;
	int posX = (screenW - dlgW) / 2;
	int posY = (screenH - dlgH) / 2;

	hwnd = CreateWindowExA(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
		wc.lpszClassName,
		"ClipBridge Setup - Language / Idioma",
		WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
		posX, posY, dlgW, dlgH,
		NULL, NULL, hInstance, NULL);

	if (!hwnd)
		return LANG_EN;

	while (GetMessageA(&msg, NULL, 0, 0) > 0) {
		if (!IsDialogMessageA(hwnd, &msg)) {
			TranslateMessage(&msg);
			DispatchMessageA(&msg);
		}
	}

	if (user_cancelled)
		return LANG_AUTO;

	return selected_lang;
}

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	(void)hPrevInstance;
	(void)lpCmdLine;
	(void)nCmdShow;

	char localAppData[MAX_PATH * 2];
	char appDir[MAX_PATH * 2];
	char destExe[MAX_PATH * 2];
	char startMenuDir[MAX_PATH * 2];
	char shortcutPath[MAX_PATH * 2];
	HRSRC hResource;
	HGLOBAL hMemory;
	DWORD exeSize;
	void *pExeData;
	FILE *fp;
	HKEY hKey;

	/* 1. Explicit Language Picker Prompt on First Install */
	enum lang_id lang_choice = prompt_user_language(hInstance);
	if (user_cancelled)
		return 0;

	i18n_init(lang_choice);

	/* 2. Prompt user to confirm installation in selected language */
	int choice = MessageBoxA(NULL,
		i18n_get(STR_INSTALL_WELCOME_MSG),
		i18n_get(STR_INSTALL_WELCOME_TITLE),
		MB_YESNO | MB_ICONQUESTION);

	if (choice != IDYES)
		return 0;

	/* 3. Determine target directories */
	if (!SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
		MessageBoxA(NULL, "Failed to resolve %LOCALAPPDATA% directory.", "Setup Error", MB_OK | MB_ICONERROR);
		return 1;
	}

	snprintf(appDir, sizeof(appDir), "%s\\ClipBridge", localAppData);
	CreateDirectoryA(appDir, NULL);
	snprintf(destExe, sizeof(destExe), "%s\\clipbridge.exe", appDir);

	/* 4. Extract embedded clipbridge.exe binary payload */
	hResource = FindResourceA(hInstance, MAKEINTRESOURCEA(2), "BINARY");
	if (hResource) {
		hMemory = LoadResource(hInstance, hResource);
		exeSize = SizeofResource(hInstance, hResource);
		pExeData = LockResource(hMemory);

		fp = fopen(destExe, "wb");
		if (fp) {
			fwrite(pExeData, 1, exeSize, fp);
			fclose(fp);
		} else {
			MessageBoxA(NULL, "Could not write to destination folder. Please close any running ClipBridge instances.", "Setup Error", MB_OK | MB_ICONERROR);
			return 1;
		}
	} else {
		/* Fallback: copy clipbridge.exe from current directory if resource is absent */
		if (!CopyFileA("clipbridge.exe", destExe, FALSE)) {
			MessageBoxA(NULL, "Could not find clipbridge.exe payload.", "Setup Error", MB_OK | MB_ICONERROR);
			return 1;
		}
	}

	/* 5. Create Start Menu Shortcut */
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROGRAMS, NULL, 0, startMenuDir))) {
		snprintf(shortcutPath, sizeof(shortcutPath), "%s\\ClipBridge.lnk", startMenuDir);
		create_shortcut(destExe, shortcutPath, i18n_get(STR_APP_DESC), destExe);
	}

	/* 6. Save chosen language to registry so ClipBridge daemon launches in it */
	if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\ClipBridge", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
		const char *langStr = (lang_choice == LANG_ES) ? "es" : "en";
		RegSetValueExA(hKey, "Language", 0, REG_SZ, (const BYTE *)langStr, (DWORD)strlen(langStr) + 1);
		RegCloseKey(hKey);
	}

	/* 7. Register in Windows Uninstall Programs registry */
	if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ClipBridge", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
		const char *name = "ClipBridge";
		const char *version = VERSION;
		const char *publisher = "Ricardo Veronese Ricci";
		char uninstCmd[MAX_PATH * 2];
		snprintf(uninstCmd, sizeof(uninstCmd), "\"%s\" --uninstall", destExe);

		RegSetValueExA(hKey, "DisplayName", 0, REG_SZ, (const BYTE *)name, (DWORD)strlen(name) + 1);
		RegSetValueExA(hKey, "DisplayVersion", 0, REG_SZ, (const BYTE *)version, (DWORD)strlen(version) + 1);
		RegSetValueExA(hKey, "Publisher", 0, REG_SZ, (const BYTE *)publisher, (DWORD)strlen(publisher) + 1);
		RegSetValueExA(hKey, "DisplayIcon", 0, REG_SZ, (const BYTE *)destExe, (DWORD)strlen(destExe) + 1);
		RegSetValueExA(hKey, "InstallLocation", 0, REG_SZ, (const BYTE *)appDir, (DWORD)strlen(appDir) + 1);
		RegSetValueExA(hKey, "UninstallString", 0, REG_SZ, (const BYTE *)uninstCmd, (DWORD)strlen(uninstCmd) + 1);
		RegCloseKey(hKey);
	}

	/* 8. Launch installed ClipBridge into System Tray */
	ShellExecuteA(NULL, "open", destExe, "-w", appDir, SW_SHOWNORMAL);

	/* 9. Success message in chosen language */
	MessageBoxA(NULL,
		i18n_get(STR_INSTALL_SUCCESS_MSG),
		i18n_get(STR_INSTALL_SUCCESS_TITLE),
		MB_OK | MB_ICONINFORMATION);

	return 0;
}

#endif
