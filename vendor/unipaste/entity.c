/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include "unipaste.h"

/* Structure for named HTML entity mappings */
struct entity_map {
	const char *name;
	const char *utf8;
};

static const struct entity_map NAMED_ENTITIES[] = {
	{ "quot", "\"" },
	{ "amp", "&" },
	{ "apos", "'" },
	{ "lt", "<" },
	{ "gt", ">" },
	{ "nbsp", " " },
	{ "ensp", " " },
	{ "emsp", "  " },
	{ "thinsp", " " },
	{ "zwnj", "" },
	{ "zwj", "" },
	{ "copy", "©" },
	{ "reg", "®" },
	{ "trade", "™" },
	{ "mdash", "—" },
	{ "ndash", "–" },
	{ "hellip", "…" },
	{ "bull", "•" },
	{ "lsquo", "‘" },
	{ "rsquo", "’" },
	{ "sbquo", "‚" },
	{ "ldquo", "“" },
	{ "rdquo", "”" },
	{ "bdquo", "„" },
	{ "dagger", "†" },
	{ "Dagger", "‡" },
	{ "permil", "‰" },
	{ "lsaquo", "‹" },
	{ "rsaquo", "›" },
	{ "euro", "€" },
	{ "pound", "£" },
	{ "yen", "¥" },
	{ "cent", "¢" },
	{ "deg", "°" },
	{ "plusmn", "±" },
	{ "sup2", "²" },
	{ "sup3", "³" },
	{ "micro", "µ" },
	{ "para", "¶" },
	{ "middot", "·" },
	{ "frac14", "¼" },
	{ "frac12", "½" },
	{ "frac34", "¾" },
	{ "times", "×" },
	{ "divide", "÷" },
	{ "sect", "§" },
	{ "check", "✓" },
	{ "cross", "✗" },
	{ "larr", "←" },
	{ "uarr", "↑" },
	{ "rarr", "→" },
	{ "darr", "↓" },
	{ "harr", "↔" },
	{ "crarr", "↵" },
	{ "infin", "∞" },
	{ "ne", "≠" },
	{ "le", "≤" },
	{ "ge", "≥" },
	{ "sum", "∑" },
	{ "prod", "∏" },
	{ "radic", "√" },
	{ "part", "∂" },
	{ "int", "∫" },
	{ "asymp", "≈" },
	{ "alpha", "α" },
	{ "beta", "β" },
	{ "gamma", "γ" },
	{ "delta", "δ" },
	{ "pi", "π" },
	{ "omega", "ω" },
	{ NULL, NULL }
};

/* Encode unicode codepoint to UTF-8 buffer */
static size_t
utf8_encode(uint32_t cp, char *out)
{
	/* Reject invalid unicode codepoints and surrogates */
	if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
		return 0;
	} else if (cp == 0xA0) {
		/* Non-breaking space -> regular space for clean plaintext */
		out[0] = ' ';
		return 1;
	} else if (cp < 0x80) {
		out[0] = (char)cp;
		return 1;
	} else if (cp < 0x800) {
		out[0] = (char)(0xC0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	} else if (cp < 0x10000) {
		out[0] = (char)(0xE0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	} else {
		out[0] = (char)(0xF0 | (cp >> 18));
		out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
		out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[3] = (char)(0x80 | (cp & 0x3F));
		return 4;
	}
}

/* Decodes a single HTML entity starting at src (which points to '&').
 * Returns number of bytes consumed from src.
 * Writes decoded UTF-8 string into dest. */
size_t
decode_html_entity(const char *src, char *dest, size_t dest_size)
{
	size_t len = 0;
	char ent_name[32];
	unsigned long val;
	char *endptr;
	size_t i;
	size_t ulen;

	if (!src || src[0] != '&' || dest_size < 8)
		return 0;

	/* Find closing ';' or max length */
	while (src[len] && src[len] != ';' && len < 31 && !isspace((unsigned char)src[len]))
		len++;

	if (src[len] != ';')
		return 0; /* Not a closed entity */

	/* Copy entity name excluding '&' and ';' */
	if (len < 2)
		return 0;
	memcpy(ent_name, src + 1, len - 1);
	ent_name[len - 1] = '\0';

	/* Numeric decimal entity: &#123; */
	if (ent_name[0] == '#' && isdigit((unsigned char)ent_name[1])) {
		errno = 0;
		val = strtoul(ent_name + 1, &endptr, 10);
		if (errno == 0 && *endptr == '\0' && val <= 0x10FFFF) {
			ulen = utf8_encode((uint32_t)val, dest);
			if (ulen > 0 && ulen < dest_size) {
				dest[ulen] = '\0';
				return len + 1;
			}
		}
	}

	/* Numeric hex entity: &#x1F600; or &#X1F600; */
	if (ent_name[0] == '#' && (ent_name[1] == 'x' || ent_name[1] == 'X') && isxdigit((unsigned char)ent_name[2])) {
		errno = 0;
		val = strtoul(ent_name + 2, &endptr, 16);
		if (errno == 0 && *endptr == '\0' && val <= 0x10FFFF) {
			ulen = utf8_encode((uint32_t)val, dest);
			if (ulen > 0 && ulen < dest_size) {
				dest[ulen] = '\0';
				return len + 1;
			}
		}
	}

	/* Named entity lookup */
	for (i = 0; NAMED_ENTITIES[i].name != NULL; i++) {
		if (strcmp(ent_name, NAMED_ENTITIES[i].name) == 0) {
			size_t elen = strlen(NAMED_ENTITIES[i].utf8);
			if (elen < dest_size) {
				memcpy(dest, NAMED_ENTITIES[i].utf8, elen + 1);
				return len + 1;
			}
		}
	}

	return 0;
}

/* Decode all HTML entities in-place in string */
void
decode_html_entities_inplace(char *str)
{
	char *r, *w;
	char decoded[16];
	size_t consumed, dlen;

	if (!str)
		return;

	r = str;
	w = str;

	while (*r) {
		if (*r == '&') {
			consumed = decode_html_entity(r, decoded, sizeof(decoded));
			if (consumed > 0) {
				dlen = strlen(decoded);
				memcpy(w, decoded, dlen);
				w += dlen;
				r += consumed;
				continue;
			}
		}
		*w++ = *r++;
	}
	*w = '\0';
}
