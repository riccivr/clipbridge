/* See LICENSE file for copyright and license details. */
#ifndef UPDATE_WIN32_H
#define UPDATE_WIN32_H

#ifdef _WIN32

#include <windows.h>
#include <shellapi.h>

/* quiet: skip "up to date"/error dialogs; still confirm when an update is available */
void update_check(HWND hwnd, NOTIFYICONDATAW *nid, int quiet);

int  update_auto_enabled(void);
void update_set_auto_enabled(int enable);

/* Rate-limited quiet check (~once/day) when auto-update is enabled */
void update_maybe_auto_check(HWND hwnd, NOTIFYICONDATAW *nid);

#endif /* _WIN32 */

#endif /* UPDATE_WIN32_H */
