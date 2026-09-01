/* See LICENSE file for copyright and license details. */
/*
 * No-op plugin: sanitize_html() returns NULL, meaning the
 * input passes through to the formatting engine unchanged.
 *
 * This file is compiled when SANITIZE=none is specified,
 * providing zero-overhead raw HTML stream processing.
 */
#include "plugin.h"

char *
sanitize_html(const char *input, size_t len)
{
	(void)input;
	(void)len;
	return NULL; /* passthrough: no sanitization */
}
