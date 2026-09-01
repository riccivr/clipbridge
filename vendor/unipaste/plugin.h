/* See LICENSE file for copyright and license details. */
#ifndef PLUGIN_H
#define PLUGIN_H

#include <stddef.h>

/*
 * Sanitize raw HTML input before parsing.
 * Returns allocated string that caller must free, or NULL on passthrough/error.
 */
char *sanitize_html(const char *input, size_t len);

#endif /* PLUGIN_H */
