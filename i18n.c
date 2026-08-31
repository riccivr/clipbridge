/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "i18n.h"

#ifdef _WIN32
#include <windows.h>
#endif

static enum lang_id current_lang = LANG_AUTO;
static enum lang_id active_lang = LANG_EN;

/* English String Table */
static const char *strings_en[STR_COUNT] = {
	[STR_APP_NAME]             = "ClipBridge",
	[STR_APP_DESC]             = "Universal Clipboard Daemon",
	[STR_PASTE_ACTIVE]         = "Paste with ClipBridge\tCtrl+Alt+V",
	[STR_AUTO_FORMAT]          = "Auto-Format Default Paste (Ctrl+V)",
	[STR_OUTPUT_MODE]          = "Output Mode",
	[STR_MODE_PLAIN]           = "Plain Text (Notepad)",
	[STR_MODE_MARKDOWN]        = "GitHub-Flavored Markdown",
	[STR_MODE_TERMINAL]        = "Terminal ANSI",
	[STR_TABLE_STYLE]          = "Table Style",
	[STR_TABLE_GRID]           = "ASCII Box (+---+)",
	[STR_TABLE_UNICODE]        = "Unicode Grid (┌───┐)",
	[STR_TABLE_MARKDOWN]       = "Markdown Pipe Table",
	[STR_TABLE_TSV]            = "Tab-Separated (TSV)",
	[STR_LANGUAGE]             = "Language / Idioma",
	[STR_LANG_AUTO]            = "System Default",
	[STR_LANG_EN]              = "English",
	[STR_LANG_ES]              = "Español (Spanish)",
	[STR_STARTUP]              = "Start with Windows",
	[STR_ABOUT_TITLE]          = "About ClipBridge",
	[STR_ABOUT_MENU]           = "About ClipBridge...",
	[STR_ABOUT_BODY]           = "ClipBridge - Universal Clipboard Daemon\n"
	                             "Version: " VERSION "\n\n"
	                             "Author: Ricardo Veronese Ricci (https://github.com/riccivr)\n"
	                             "Icon Design: Estefani Medina (https://github.com/estephmediseno)\n"
	                             "Project: https://github.com/riccivr/clipbridge\n\n"
	                             "A lightweight clipboard formatting bridge powered by unipaste.\n"
	                             "License: MIT License",
	[STR_EXIT]                 = "Exit ClipBridge",
	[STR_TOOLTIP_ACTIVE]       = "ClipBridge (Press Ctrl+Alt+V to paste)",
	[STR_TOOLTIP_AUTO]         = "ClipBridge (Auto-format Ctrl+V enabled)",
	[STR_INSTALL_WELCOME_TITLE]= "ClipBridge Setup",
	[STR_INSTALL_WELCOME_MSG]  = "Welcome to ClipBridge Setup!\n\n"
	                             "This will install ClipBridge on your computer and create a Start Menu launcher.\n\n"
	                             "Do you want to continue?",
	[STR_INSTALL_SUCCESS_TITLE]= "ClipBridge Setup Complete",
	[STR_INSTALL_SUCCESS_MSG]  = "ClipBridge has been installed successfully!\n\n"
	                             "• You can now launch ClipBridge anytime from your Windows Start Menu.\n"
	                             "• ClipBridge is now active in your System Tray.\n"
	                             "• Press Ctrl+Alt+V anywhere to paste clean formatted text."
};

/* Spanish String Table */
static const char *strings_es[STR_COUNT] = {
	[STR_APP_NAME]             = "ClipBridge",
	[STR_APP_DESC]             = "Demonio universal de portapapeles",
	[STR_PASTE_ACTIVE]         = "Pegar con ClipBridge\tCtrl+Alt+V",
	[STR_AUTO_FORMAT]          = "Formateo automático al pegar (Ctrl+V)",
	[STR_OUTPUT_MODE]          = "Modo de salida",
	[STR_MODE_PLAIN]           = "Texto plano (Bloc de notas)",
	[STR_MODE_MARKDOWN]        = "Markdown de GitHub",
	[STR_MODE_TERMINAL]        = "Terminal ANSI",
	[STR_TABLE_STYLE]          = "Estilo de tablas",
	[STR_TABLE_GRID]           = "Cuadrícula ASCII (+---+)",
	[STR_TABLE_UNICODE]        = "Cuadrícula Unicode (┌───┐)",
	[STR_TABLE_MARKDOWN]       = "Tabla Markdown",
	[STR_TABLE_TSV]            = "Valores separados por tabuladores (TSV)",
	[STR_LANGUAGE]             = "Idioma / Language",
	[STR_LANG_AUTO]            = "Predeterminado del sistema",
	[STR_LANG_EN]              = "English (Inglés)",
	[STR_LANG_ES]              = "Español",
	[STR_STARTUP]              = "Iniciar con Windows",
	[STR_ABOUT_TITLE]          = "Acerca de ClipBridge",
	[STR_ABOUT_MENU]           = "Acerca de ClipBridge...",
	[STR_ABOUT_BODY]           = "ClipBridge - Demonio universal de portapapeles\n"
	                             "Versión: " VERSION "\n\n"
	                             "Autor: Ricardo Veronese Ricci (https://github.com/riccivr)\n"
	                             "Diseño del icono: Estefani Medina (https://github.com/estephmediseno)\n"
	                             "Proyecto: https://github.com/riccivr/clipbridge\n\n"
	                             "Un puente ligero de formateo de portapapeles impulsado por unipaste.\n"
	                             "Licencia: Licencia MIT",
	[STR_EXIT]                 = "Salir de ClipBridge",
	[STR_TOOLTIP_ACTIVE]       = "ClipBridge (Pulsa Ctrl+Alt+V para pegar)",
	[STR_TOOLTIP_AUTO]         = "ClipBridge (Formateo automático Ctrl+V activo)",
	[STR_INSTALL_WELCOME_TITLE]= "Instalación de ClipBridge",
	[STR_INSTALL_WELCOME_MSG]  = "¡Bienvenido a la instalación de ClipBridge!\n\n"
	                             "Esto instalará ClipBridge en tu equipo y creará un acceso directo en el Menú Inicio.\n\n"
	                             "¿Deseas continuar?",
	[STR_INSTALL_SUCCESS_TITLE]= "Instalación de ClipBridge completada",
	[STR_INSTALL_SUCCESS_MSG]  = "¡ClipBridge se ha instalado correctamente!\n\n"
	                             "• Ahora puedes iniciar ClipBridge en cualquier momento desde el Menú Inicio.\n"
	                             "• ClipBridge está activo en la bandeja del sistema.\n"
	                             "• Pulsa Ctrl+Alt+V en cualquier lugar para pegar texto limpio y formateado."
};

static enum lang_id
detect_system_language(void)
{
#ifdef _WIN32
	LANGID langId = GetUserDefaultUILanguage();
	if (PRIMARYLANGID(langId) == LANG_SPANISH) {
		return LANG_ES;
	}
#else
	const char *lang_env = getenv("LANG");
	if (!lang_env)
		lang_env = getenv("LC_MESSAGES");
	if (lang_env && strncmp(lang_env, "es", 2) == 0) {
		return LANG_ES;
	}
#endif
	return LANG_EN;
}

void
i18n_init(enum lang_id lang)
{
	i18n_set_language(lang);
}

void
i18n_set_language(enum lang_id lang)
{
	current_lang = lang;
	if (lang == LANG_AUTO) {
		active_lang = detect_system_language();
	} else {
		active_lang = lang;
	}
}

enum lang_id
i18n_get_language(void)
{
	return current_lang;
}

const char *
i18n_get(enum str_id id)
{
	if (id < 0 || id >= STR_COUNT)
		return "";

	if (active_lang == LANG_ES && strings_es[id]) {
		return strings_es[id];
	}
	return strings_en[id] ? strings_en[id] : "";
}
