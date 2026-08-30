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
	ACTION_PASTE
};

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-w1pruv] [-m mode] [-t table] [-l link]\n", argv0);
	fprintf(stderr, "\nActions:\n");
	fprintf(stderr, "  -w         Watch clipboard continuously and auto-sync (default)\n");
	fprintf(stderr, "  -1         Perform single clipboard synchronization and exit\n");
	fprintf(stderr, "  -p         Print formatted clipboard content directly to stdout\n");
	fprintf(stderr, "\nFormatting Options:\n");
	fprintf(stderr, "  -m mode    Output mode: plain (default), markdown, terminal\n");
	fprintf(stderr, "  -t table   Table format: grid (default), markdown, tsv, simple\n");
	fprintf(stderr, "  -l link    Link format: bracket (default), inline, text, footnote\n");
	fprintf(stderr, "  -u         Use Unicode box-drawing characters for tables\n");
	fprintf(stderr, "  -r         Emit Windows CRLF (\\r\\n) line endings\n");
	fprintf(stderr, "  -v         Display version information\n");
	fprintf(stderr, "  -h         Display this help message\n");
	exit(1);
}

#ifdef _WIN32
#include <windows.h>
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
	case 'p':
		action = ACTION_PASTE;
		break;
	case 'm':
		arg = EARGF(usage());
		if (strcmp(arg, "plain") == 0)
			cfg.mode = MODE_PLAIN;
		else if (strcmp(arg, "markdown") == 0 || strcmp(arg, "md") == 0)
			cfg.mode = MODE_MARKDOWN;
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
	case ACTION_WATCH:
	default:
		return clipboard_watch(&cfg);
	}
}
