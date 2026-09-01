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

echo ""
echo "======================================"
echo "Results: $PASSED passed, $FAILED failed"
echo "======================================"

if [ "$FAILED" -ne 0 ]; then
	exit 1
fi
