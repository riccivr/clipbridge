#!/bin/sh
set -e
PASSED=0
FAILED=0
EXE="./clipbridge"

test_contains() {
	NAME="$1"
	INPUT="$2"
	SUBSTRING="$3"
	ARGS="$4"

	if [ -n "$INPUT" ]; then
		ACTUAL=$(printf "%s" "$INPUT" | $EXE $ARGS 2>&1 || true)
	else
		ACTUAL=$($EXE $ARGS 2>&1 || true)
	fi

	case "$ACTUAL" in
		*"$SUBSTRING"*)
			printf "[PASS] %s\n" "$NAME"
			PASSED=$((PASSED + 1))
			;;
		*)
			printf "[FAIL] %s (missing '%s')\n=== Actual ===\n%s\n" "$NAME" "$SUBSTRING" "$ACTUAL"
			FAILED=$((FAILED + 1))
			;;
	esac
}

test_not_contains() {
	NAME="$1"
	INPUT="$2"
	FORBIDDEN="$3"
	ARGS="$4"

	if [ -n "$INPUT" ]; then
		ACTUAL=$(printf "%s" "$INPUT" | $EXE $ARGS 2>&1 || true)
	else
		ACTUAL=$($EXE $ARGS 2>&1 || true)
	fi

	case "$ACTUAL" in
		*"$FORBIDDEN"*)
			printf "[FAIL] %s (found '%s')\n=== Actual ===\n%s\n" "$NAME" "$FORBIDDEN" "$ACTUAL"
			FAILED=$((FAILED + 1))
			;;
		*)
			printf "[PASS] %s\n" "$NAME"
			PASSED=$((PASSED + 1))
			;;
	esac
}

echo "Running clipbridge integration test suite..."

test_contains "CLI: Version" "" "clipbridge-1.3.0" "-v"
test_contains "CLI: Help" "" "usage:" "-h"
test_contains "CLI: Unknown flag" "" "usage:" "-z"

HTML_SAMPLE='<h1>Release Plan</h1><p>Check the <a href="https://example.com?utm_source=slack&utm_medium=chat">dashboard</a> for details.</p><table><tr><th>Item</th><th>Status</th></tr><tr><td>Core Engine</td><td><b>Ready</b></td></tr></table>'

test_contains "HTML: Markdown mode" "$HTML_SAMPLE" "[dashboard](https://example.com)" "-p -m markdown"
test_not_contains "HTML: Strip tracking" "$HTML_SAMPLE" "utm_source" "-p -m markdown"
test_contains "HTML: Keep tracking" "$HTML_SAMPLE" "utm_source=slack" "-p -m markdown -K"
test_contains "HTML: Table in Markdown" "$HTML_SAMPLE" "| Core Engine | **Ready** |" "-p -m markdown"
test_contains "HTML: Slack heading" "$HTML_SAMPLE" "*Release Plan*" "-p -m slack"
test_contains "HTML: Slack link" "$HTML_SAMPLE" "<https://example.com|dashboard>" "-p -m slack"
test_contains "HTML: Jira heading" "$HTML_SAMPLE" "h1. Release Plan" "-p -m jira"
test_contains "HTML: Jira table header" "$HTML_SAMPLE" "|| Item || Status ||" "-p -m jira"
test_contains "HTML: Jira link" "$HTML_SAMPLE" "[dashboard|https://example.com]" "-p -m jira"
test_contains "HTML: Unicode table box" "$HTML_SAMPLE" "┌" "-p -u"

SCRIPT_ATTACK='<p>Safe content</p><script type="text/javascript">alert("xss")</script><p>More safe content</p>'
test_contains "Security: Content preserved" "$SCRIPT_ATTACK" "Safe content" "-p -m markdown"
test_not_contains "Security: Script stripped" "$SCRIPT_ATTACK" "alert" "-p -m markdown"

RAW_TSV=$(printf "Col1\tCol2\tCol3\nVal1\tVal2\tVal3\n")
test_contains "TSV: Grid conversion" "$RAW_TSV" "| Col1 | Col2 | Col3 |" "-p -m markdown"

# Test 10: Windows CF_HTML header stripping
CF_HTML_RAW=$(printf "Version:0.9\r\nStartHTML:0000000105\r\nEndHTML:0000008959\r\nStartFragment:0000000141\r\nEndFragment:0000008923\r\n<html><body><!--StartFragment--><h1>Clean Title</h1><p>Paragraph content</p><!--EndFragment--></body></html>")
test_contains "CF_HTML: Content parsed" "$CF_HTML_RAW" "# Clean Title" "-p -m markdown"
test_not_contains "CF_HTML: Header Version stripped" "$CF_HTML_RAW" "Version:0.9" "-p -m markdown"
test_not_contains "CF_HTML: Header StartFragment stripped" "$CF_HTML_RAW" "StartFragment" "-p -m markdown"

# Test 11: Live clipboard synchronization test (if display is active)
if [ -n "$WAYLAND_DISPLAY" ] && command -v wl-copy >/dev/null 2>&1 && command -v wl-paste >/dev/null 2>&1; then
	printf "<b>Live</b>" | wl-copy -t text/html
	$EXE -1 -m markdown
	TEXT_OUT=$(wl-paste -t text/plain 2>/dev/null || true)
	if [ "$TEXT_OUT" = "**Live**" ]; then
		printf "[PASS] Live Clipboard (Wayland): Formatted plain text updated\n"
		PASSED=$((PASSED + 1))
	else
		printf "[FAIL] Live Clipboard (Wayland): Expected '**Live**', got '%s'\n" "$TEXT_OUT"
		FAILED=$((FAILED + 1))
	fi
	if [ -n "$DISPLAY" ] && command -v xclip >/dev/null 2>&1; then
		HTML_OUT=$(xclip -selection clipboard -t text/html -o 2>/dev/null || true)
		case "$HTML_OUT" in
			*"<b>Live</b>"*|*"<b>Live</b>"*)
				printf "[PASS] Live Clipboard (XWayland): HTML slot preserved\n"
				PASSED=$((PASSED + 1))
				;;
			*)
				printf "[FAIL] Live Clipboard (XWayland): HTML slot lost\n"
				FAILED=$((FAILED + 1))
				;;
		esac
	fi
elif [ -n "$DISPLAY" ] && command -v xclip >/dev/null 2>&1; then
	printf "<b>Live</b>" | xclip -selection clipboard -t text/html
	$EXE -1 -m markdown
	TEXT_OUT=$(xclip -selection clipboard -o 2>/dev/null || true)
	HTML_OUT=$(xclip -selection clipboard -t text/html -o 2>/dev/null || true)
	if [ "$TEXT_OUT" = "**Live**" ]; then
		printf "[PASS] Live Clipboard (X11): Formatted plain text updated\n"
		PASSED=$((PASSED + 1))
	else
		printf "[FAIL] Live Clipboard (X11): Expected '**Live**', got '%s'\n" "$TEXT_OUT"
		FAILED=$((FAILED + 1))
	fi
	case "$HTML_OUT" in
		*"<b>Live</b>"*)
			printf "[PASS] Live Clipboard (X11): HTML slot preserved\n"
			PASSED=$((PASSED + 1))
			;;
		*)
			printf "[FAIL] Live Clipboard (X11): HTML slot lost (got '%s')\n" "$HTML_OUT"
			FAILED=$((FAILED + 1))
			;;
	esac
else
	printf "[SKIP] Live Clipboard test: No active GUI/display session detected in this test run\n"
fi

# Test 12: Watch backend starts and names its event source (CI, no GUI required)
if command -v timeout >/dev/null 2>&1; then
	WATCH_LOG=$(timeout 1 "$EXE" -w 2>&1 || true)
	case "$WATCH_LOG" in
		*"watching Wayland clipboard via wl-paste --watch"*|*"watching X11 CLIPBOARD via XFixes"*|*"watching X11 clipboard via clipnotify"*|*"no event source available, polling every 250ms"*)
			printf "[PASS] Watch: announced an event backend\n"
			PASSED=$((PASSED + 1))
			;;
		*"another instance is already running"*)
			printf "[SKIP] Watch: another clipbridge instance is running\n"
			;;
		*)
			printf "[FAIL] Watch: did not announce a backend\n=== Actual ===\n%s\n" "$WATCH_LOG"
			FAILED=$((FAILED + 1))
			;;
	esac
else
	printf "[SKIP] Watch: timeout(1) not available\n"
fi

echo ""
echo "======================================"
echo "Results: $PASSED passed, $FAILED failed"
echo "======================================"

if [ "$FAILED" -ne 0 ]; then
	exit 1
fi
