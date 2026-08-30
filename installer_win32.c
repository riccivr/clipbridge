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

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	(void)hInstance;
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

	i18n_init(LANG_AUTO);

	/* 1. Prompt user to confirm installation */
	int choice = MessageBoxA(NULL,
		i18n_get(STR_INSTALL_WELCOME_MSG),
		i18n_get(STR_INSTALL_WELCOME_TITLE),
		MB_YESNO | MB_ICONQUESTION);

	if (choice != IDYES)
		return 0;

	/* 2. Determine target directories */
	if (!SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
		MessageBoxA(NULL, "Failed to resolve %LOCALAPPDATA% directory.", "Setup Error", MB_OK | MB_ICONERROR);
		return 1;
	}

	snprintf(appDir, sizeof(appDir), "%s\\ClipBridge", localAppData);
	CreateDirectoryA(appDir, NULL);
	snprintf(destExe, sizeof(destExe), "%s\\clipbridge.exe", appDir);

	/* 3. Extract embedded clipbridge.exe binary payload */
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

	/* 4. Create Start Menu Shortcut */
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROGRAMS, NULL, 0, startMenuDir))) {
		snprintf(shortcutPath, sizeof(shortcutPath), "%s\\ClipBridge.lnk", startMenuDir);
		create_shortcut(destExe, shortcutPath, i18n_get(STR_APP_DESC), destExe);
	}

	/* 5. Register in Windows Uninstall Programs registry */
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

	/* 6. Launch installed ClipBridge into System Tray */
	ShellExecuteA(NULL, "open", destExe, "-w", appDir, SW_SHOWNORMAL);

	/* 7. Success message */
	MessageBoxA(NULL,
		i18n_get(STR_INSTALL_SUCCESS_MSG),
		i18n_get(STR_INSTALL_SUCCESS_TITLE),
		MB_OK | MB_ICONINFORMATION);

	return 0;
}

#endif
