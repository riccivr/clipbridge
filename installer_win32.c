/* See LICENSE file for copyright and license details. */
#ifdef _WIN32

#define _WIN32_WINNT 0x0600
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "i18n.h"

#define VERSION "1.2.0"

static int user_cancelled = 0;

static int
create_shortcut_w(const wchar_t *target_path, const wchar_t *shortcut_path, const wchar_t *description, const wchar_t *icon_path)
{
	IShellLinkW *pShellLink = NULL;
	IPersistFile *pPersistFile = NULL;
	HRESULT hr;

	CoInitialize(NULL);
	hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&pShellLink);
	if (SUCCEEDED(hr)) {
		pShellLink->lpVtbl->SetPath(pShellLink, target_path);
		pShellLink->lpVtbl->SetDescription(pShellLink, description);
		if (icon_path) {
			pShellLink->lpVtbl->SetIconLocation(pShellLink, icon_path, 0);
		}

		hr = pShellLink->lpVtbl->QueryInterface(pShellLink, &IID_IPersistFile, (void **)&pPersistFile);
		if (SUCCEEDED(hr)) {
			hr = pPersistFile->lpVtbl->Save(pPersistFile, shortcut_path, TRUE);
			pPersistFile->lpVtbl->Release(pPersistFile);
		}
		pShellLink->lpVtbl->Release(pShellLink);
	}
	CoUninitialize();
	return SUCCEEDED(hr);
}

typedef HRESULT (WINAPI *PFN_TaskDialogIndirect)(
	const TASKDIALOGCONFIG *pTaskConfig,
	int *pnButton,
	int *pnRadioButton,
	BOOL *pfVerificationFlagChecked
);

static enum lang_id
prompt_user_language_taskdialog(HINSTANCE hInstance)
{
	HMODULE hComCtl = LoadLibraryW(L"comctl32.dll");
	if (hComCtl) {
		PFN_TaskDialogIndirect pfnTaskDialogIndirect = 
			(PFN_TaskDialogIndirect)GetProcAddress(hComCtl, "TaskDialogIndirect");

		if (pfnTaskDialogIndirect) {
			TASKDIALOG_BUTTON buttons[] = {
				{ 101, L"English\nInstall ClipBridge with English interface" },
				{ 102, L"Espa\u00f1ol\nInstalar ClipBridge con interfaz en espa\u00f1ol" }
			};

			TASKDIALOGCONFIG tc;
			memset(&tc, 0, sizeof(tc));
			tc.cbSize = sizeof(tc);
			tc.hInstance = hInstance;
			tc.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_CAN_BE_MINIMIZED | TDF_USE_HICON_MAIN;
			tc.hMainIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
			tc.pszWindowTitle = L"ClipBridge Setup";
			tc.pszMainInstruction = L"Select Language / Seleccione el idioma";
			tc.pszContent = L"Choose your preferred language for ClipBridge:\nElija su idioma preferido para ClipBridge:";
			tc.cButtons = 2;
			tc.pButtons = buttons;
			tc.dwCommonButtons = TDCBF_CANCEL_BUTTON;

			int clickedButton = 0;
			HRESULT hr = pfnTaskDialogIndirect(&tc, &clickedButton, NULL, NULL);
			if (SUCCEEDED(hr)) {
				if (clickedButton == 101) return LANG_EN;
				if (clickedButton == 102) return LANG_ES;
				if (clickedButton == IDCANCEL) {
					user_cancelled = 1;
					return LANG_AUTO;
				}
			}
		}
	}

	return (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_SPANISH) ? LANG_ES : LANG_EN;
}

static int
show_welcome_taskdialog(HINSTANCE hInstance, enum lang_id lang)
{
	HMODULE hComCtl = LoadLibraryW(L"comctl32.dll");
	if (hComCtl) {
		PFN_TaskDialogIndirect pfnTaskDialogIndirect = 
			(PFN_TaskDialogIndirect)GetProcAddress(hComCtl, "TaskDialogIndirect");

		if (pfnTaskDialogIndirect) {
			TASKDIALOG_BUTTON buttons[] = {
				{ 201, (lang == LANG_ES)
					? L"Instalar ClipBridge\nInstalar en %LOCALAPPDATA%\\ClipBridge e iniciar demonio"
					: L"Install ClipBridge\nInstall to %LOCALAPPDATA%\\ClipBridge and launch daemon" }
			};

			TASKDIALOGCONFIG tc;
			memset(&tc, 0, sizeof(tc));
			tc.cbSize = sizeof(tc);
			tc.hInstance = hInstance;
			tc.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_CAN_BE_MINIMIZED | TDF_USE_HICON_MAIN;
			tc.hMainIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
			tc.pszWindowTitle = L"ClipBridge Setup";
			tc.pszMainInstruction = (lang == LANG_ES) ? L"\u00a1Bienvenido a la instalaci\u00f3n de ClipBridge!" : L"Welcome to ClipBridge Setup!";
			tc.pszContent = (lang == LANG_ES)
				? L"ClipBridge es un demonio universal de portapapeles impulsado por unipaste.\n\nEsto instalar\u00e1 la aplicaci\u00f3n y crear\u00e1 un acceso directo en el Men\u00fa Inicio."
				: L"ClipBridge is a universal clipboard bridge and background daemon powered by unipaste.\n\nThis will install the application and create a Start Menu launcher.";
			tc.cButtons = 1;
			tc.pButtons = buttons;
			tc.dwCommonButtons = TDCBF_CANCEL_BUTTON;

			int clickedButton = 0;
			HRESULT hr = pfnTaskDialogIndirect(&tc, &clickedButton, NULL, NULL);
			if (SUCCEEDED(hr)) {
				return (clickedButton == 201);
			}
		}
	}
	return 1;
}

static void
show_success_taskdialog(HINSTANCE hInstance, enum lang_id lang)
{
	HMODULE hComCtl = LoadLibraryW(L"comctl32.dll");
	if (hComCtl) {
		PFN_TaskDialogIndirect pfnTaskDialogIndirect = 
			(PFN_TaskDialogIndirect)GetProcAddress(hComCtl, "TaskDialogIndirect");

		if (pfnTaskDialogIndirect) {
			TASKDIALOGCONFIG tc;
			memset(&tc, 0, sizeof(tc));
			tc.cbSize = sizeof(tc);
			tc.hInstance = hInstance;
			tc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_CAN_BE_MINIMIZED | TDF_USE_HICON_MAIN;
			tc.hMainIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
			tc.pszWindowTitle = L"ClipBridge Setup";
			tc.pszMainInstruction = (lang == LANG_ES) ? L"\u00a1ClipBridge se ha instalado correctamente!" : L"ClipBridge Installed Successfully!";
			tc.pszContent = (lang == LANG_ES)
				? L"\u2022 ClipBridge est\u00e1 activo en la bandeja del sistema.\n\u2022 Pulsa Ctrl+Alt+V en cualquier lugar para pegar texto limpio y formateado.\n\u2022 Puedes iniciar ClipBridge en cualquier momento desde el Men\u00fa Inicio."
				: L"\u2022 ClipBridge is now active in your System Tray.\n\u2022 Press Ctrl+Alt+V anywhere to paste clean formatted text.\n\u2022 Launch anytime from your Windows Start Menu.";
			tc.dwCommonButtons = TDCBF_OK_BUTTON;

			pfnTaskDialogIndirect(&tc, NULL, NULL, NULL);
			return;
		}
	}

	MessageBoxW(NULL,
		(lang == LANG_ES) ? L"¡ClipBridge se ha instalado correctamente!" : L"ClipBridge has been installed successfully!",
		L"ClipBridge Setup",
		MB_OK | MB_ICONINFORMATION);
}

static int
perform_windows_uninstallation(HINSTANCE hInstance)
{
	/* 1. Detect saved language preference */
	enum lang_id lang = LANG_EN;
	HKEY hKey;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\ClipBridge", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
		wchar_t buf[32];
		DWORD len = sizeof(buf);
		if (RegQueryValueExW(hKey, L"Language", NULL, NULL, (LPBYTE)buf, &len) == ERROR_SUCCESS) {
			if (wcscmp(buf, L"es") == 0) lang = LANG_ES;
		}
		RegCloseKey(hKey);
	}

	/* 2. Ask confirmation via TaskDialog */
	HMODULE hComCtl = LoadLibraryW(L"comctl32.dll");
	if (hComCtl) {
		PFN_TaskDialogIndirect pfnTaskDialogIndirect = 
			(PFN_TaskDialogIndirect)GetProcAddress(hComCtl, "TaskDialogIndirect");

		if (pfnTaskDialogIndirect) {
			TASKDIALOG_BUTTON buttons[] = {
				{ 101, (lang == LANG_ES)
					? L"Desinstalar ClipBridge\nEliminar ClipBridge y todos sus componentes"
					: L"Uninstall ClipBridge\nRemove ClipBridge and all its components" }
			};

			TASKDIALOGCONFIG tc;
			memset(&tc, 0, sizeof(tc));
			tc.cbSize = sizeof(tc);
			tc.hInstance = hInstance;
			tc.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_CAN_BE_MINIMIZED | TDF_USE_HICON_MAIN;
			tc.hMainIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
			tc.pszWindowTitle = L"ClipBridge Uninstall";
			tc.pszMainInstruction = (lang == LANG_ES) ? L"\u00bfDesea desinstalar ClipBridge?" : L"Do you want to uninstall ClipBridge?";
			tc.pszContent = (lang == LANG_ES)
				? L"Esto cerrar\u00e1 cualquier instancia activa y eliminar\u00e1 ClipBridge de su equipo."
				: L"This will close any active background instance and remove ClipBridge from your computer.";
			tc.cButtons = 1;
			tc.pButtons = buttons;
			tc.dwCommonButtons = TDCBF_CANCEL_BUTTON;

			int clickedButton = 0;
			HRESULT hr = pfnTaskDialogIndirect(&tc, &clickedButton, NULL, NULL);
			if (SUCCEEDED(hr)) {
				if (clickedButton != 101) return 0;
			}
		}
	}

	/* 3. Terminate running daemon instance */
	HWND existing = FindWindowA("ClipBridgeMonitorClass", "ClipBridge");
	if (existing) {
		PostMessageA(existing, WM_COMMAND, 1015 /* ID_TRAY_EXIT */, 0);
		Sleep(400);
	}

	/* 4. Remove Start Menu Shortcut */
	wchar_t startMenuDir[MAX_PATH * 2];
	if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, startMenuDir))) {
		wchar_t shortcutPath[MAX_PATH * 2];
		_snwprintf(shortcutPath, sizeof(shortcutPath) / sizeof(wchar_t), L"%ls\\ClipBridge.lnk", startMenuDir);
		DeleteFileW(shortcutPath);
	}

	/* 5. Remove Run at Startup registry value */
	if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
		RegDeleteValueW(hKey, L"ClipBridge");
		RegCloseKey(hKey);
	}

	/* 6. Remove Uninstall registry entries */
	RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ClipBridge");
	RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\ClipBridge");

	/* 7. Remove AppData directory */
	wchar_t localAppData[MAX_PATH * 2];
	if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
		wchar_t appDir[MAX_PATH * 2];
		_snwprintf(appDir, sizeof(appDir) / sizeof(wchar_t), L"%ls\\ClipBridge", localAppData);
		wchar_t exePath[MAX_PATH * 2];
		_snwprintf(exePath, sizeof(exePath) / sizeof(wchar_t), L"%ls\\clipbridge.exe", appDir);
		DeleteFileW(exePath);
		RemoveDirectoryW(appDir);

		/* Background fallback self-delete */
		char cmd[MAX_PATH * 3];
		snprintf(cmd, sizeof(cmd), "/C ping 127.0.0.1 -n 2 > nul & rd /S /Q \"%ls\"", appDir);
		ShellExecuteA(NULL, "open", "cmd.exe", cmd, NULL, SW_HIDE);
	}

	/* 8. Success notification */
	if (hComCtl) {
		PFN_TaskDialogIndirect pfnTaskDialogIndirect = 
			(PFN_TaskDialogIndirect)GetProcAddress(hComCtl, "TaskDialogIndirect");

		if (pfnTaskDialogIndirect) {
			TASKDIALOGCONFIG tc;
			memset(&tc, 0, sizeof(tc));
			tc.cbSize = sizeof(tc);
			tc.hInstance = hInstance;
			tc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_CAN_BE_MINIMIZED | TDF_USE_HICON_MAIN;
			tc.hMainIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
			tc.pszWindowTitle = L"ClipBridge Uninstall";
			tc.pszMainInstruction = (lang == LANG_ES) ? L"\u00a1ClipBridge se ha desinstalado correctamente!" : L"ClipBridge Uninstalled Successfully!";
			tc.pszContent = (lang == LANG_ES)
				? L"ClipBridge y todos sus componentes han sido eliminados de su equipo."
				: L"ClipBridge and all its components have been removed from your computer.";
			tc.dwCommonButtons = TDCBF_OK_BUTTON;
			pfnTaskDialogIndirect(&tc, NULL, NULL, NULL);
			return 0;
		}
	}

	MessageBoxW(NULL,
		(lang == LANG_ES) ? L"¡ClipBridge se ha desinstalado correctamente!" : L"ClipBridge has been uninstalled successfully!",
		L"ClipBridge Uninstall",
		MB_OK | MB_ICONINFORMATION);

	return 0;
}

int WINAPI
wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	(void)hPrevInstance;
	(void)nCmdShow;

	INITCOMMONCONTROLSEX icex;
	icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
	icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
	InitCommonControlsEx(&icex);

	/* Check for --uninstall flag */
	if (pCmdLine && (wcsstr(pCmdLine, L"--uninstall") != NULL || wcsstr(pCmdLine, L"-u") != NULL || wcsstr(pCmdLine, L"/uninstall") != NULL)) {
		return perform_windows_uninstallation(hInstance);
	}

	wchar_t localAppData[MAX_PATH * 2];
	wchar_t appDir[MAX_PATH * 2];
	wchar_t destExe[MAX_PATH * 2];
	wchar_t startMenuDir[MAX_PATH * 2];
	wchar_t shortcutPath[MAX_PATH * 2];
	HRSRC hResource;
	HGLOBAL hMemory;
	DWORD exeSize;
	void *pExeData;
	FILE *fp;
	HKEY hKey;

	/* 1. Modern Native TaskDialog Language Selection */
	enum lang_id lang_choice = prompt_user_language_taskdialog(hInstance);
	if (user_cancelled)
		return 0;

	/* 2. Welcome & Installation Confirmation Dialog */
	if (!show_welcome_taskdialog(hInstance, lang_choice))
		return 0;

	/* 3. Determine target directories */
	if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
		MessageBoxW(NULL, L"Failed to resolve %LOCALAPPDATA% directory.", L"Setup Error", MB_OK | MB_ICONERROR);
		return 1;
	}

	_snwprintf(appDir, sizeof(appDir) / sizeof(wchar_t), L"%ls\\ClipBridge", localAppData);
	CreateDirectoryW(appDir, NULL);
	_snwprintf(destExe, sizeof(destExe) / sizeof(wchar_t), L"%ls\\clipbridge.exe", appDir);

	/* 4. Extract embedded binary payload */
	hResource = FindResourceW(hInstance, MAKEINTRESOURCEW(2), L"BINARY");
	if (hResource) {
		hMemory = LoadResource(hInstance, hResource);
		exeSize = SizeofResource(hInstance, hResource);
		pExeData = LockResource(hMemory);

		fp = _wfopen(destExe, L"wb");
		if (fp) {
			fwrite(pExeData, 1, exeSize, fp);
			fclose(fp);
		} else {
			MessageBoxW(NULL,
				(lang_choice == LANG_ES)
					? L"No se pudo escribir en la carpeta de destino. Por favor cierre cualquier instancia abierta de ClipBridge."
					: L"Could not write to destination folder. Please close any running ClipBridge instances.",
				L"Setup Error", MB_OK | MB_ICONERROR);
			return 1;
		}
	} else {
		/* Fallback: copy from current directory */
		if (!CopyFileW(L"clipbridge-portable.exe", destExe, FALSE) && !CopyFileW(L"clipbridge.exe", destExe, FALSE)) {
			MessageBoxW(NULL, L"Could not find clipbridge payload.", L"Setup Error", MB_OK | MB_ICONERROR);
			return 1;
		}
	}

	/* 5. Create Start Menu Shortcut */
	if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, startMenuDir))) {
		_snwprintf(shortcutPath, sizeof(shortcutPath) / sizeof(wchar_t), L"%ls\\ClipBridge.lnk", startMenuDir);
		create_shortcut_w(destExe, shortcutPath,
			(lang_choice == LANG_ES) ? L"Demonio universal de portapapeles" : L"Universal Clipboard Daemon",
			destExe);
	}

	/* 6. Save chosen language to registry */
	if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\ClipBridge", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
		const wchar_t *langStr = (lang_choice == LANG_ES) ? L"es" : L"en";
		RegSetValueExW(hKey, L"Language", 0, REG_SZ, (const BYTE *)langStr, (DWORD)((wcslen(langStr) + 1) * sizeof(wchar_t)));
		RegCloseKey(hKey);
	}

	/* 7. Register in Windows Uninstall Programs */
	if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ClipBridge", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
		const wchar_t *name = L"ClipBridge";
		const wchar_t *version = L"1.2.0";
		const wchar_t *publisher = L"Ricardo Veronese Ricci";
		wchar_t uninstCmd[MAX_PATH * 2];
		_snwprintf(uninstCmd, sizeof(uninstCmd) / sizeof(wchar_t), L"\"%ls\" --uninstall", destExe);

		RegSetValueExW(hKey, L"DisplayName", 0, REG_SZ, (const BYTE *)name, (DWORD)((wcslen(name) + 1) * sizeof(wchar_t)));
		RegSetValueExW(hKey, L"DisplayVersion", 0, REG_SZ, (const BYTE *)version, (DWORD)((wcslen(version) + 1) * sizeof(wchar_t)));
		RegSetValueExW(hKey, L"Publisher", 0, REG_SZ, (const BYTE *)publisher, (DWORD)((wcslen(publisher) + 1) * sizeof(wchar_t)));
		RegSetValueExW(hKey, L"DisplayIcon", 0, REG_SZ, (const BYTE *)destExe, (DWORD)((wcslen(destExe) + 1) * sizeof(wchar_t)));
		RegSetValueExW(hKey, L"InstallLocation", 0, REG_SZ, (const BYTE *)appDir, (DWORD)((wcslen(appDir) + 1) * sizeof(wchar_t)));
		RegSetValueExW(hKey, L"UninstallString", 0, REG_SZ, (const BYTE *)uninstCmd, (DWORD)((wcslen(uninstCmd) + 1) * sizeof(wchar_t)));
		RegCloseKey(hKey);
	}

	/* 8. Launch installed ClipBridge into System Tray */
	ShellExecuteW(NULL, L"open", destExe, L"-w", appDir, SW_SHOWNORMAL);

	/* 9. Modern Success Dialog */
	show_success_taskdialog(hInstance, lang_choice);

	return 0;
}

#endif
