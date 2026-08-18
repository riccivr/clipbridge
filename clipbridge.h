/* See LICENSE file for copyright and license details. */
#ifndef CLIPBRIDGE_H
#define CLIPBRIDGE_H

#include <stddef.h>
#include <stdio.h>
#include "unipaste.h"

/* Platform clipboard API */
int clipboard_read_html(char **out_html, size_t *out_len);
int clipboard_write_text(const char *text, size_t len);
int clipboard_sync_once(const struct config *cfg);
int clipboard_paste_stdout(const struct config *cfg);
int clipboard_watch(const struct config *cfg);

#endif /* CLIPBRIDGE_H */
