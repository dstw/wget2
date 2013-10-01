/*
 * Copyright(c) 2012 Tim Ruehsen
 *
 * This file is part of libmget.
 *
 * Libmget is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Libmget is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libmget.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * URI/IRI routines
 * about encoding see http://nikitathespider.com/articles/EncodingDivination.html
 * about GET encoding see http://stackoverflow.com/questions/1549213/whats-the-correct-encoding-of-http-get-request-strings
 *
 *
 * Changelog
 * 25.04.2012  Tim Ruehsen  created
 *
 */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <string.h>
#include <ctype.h>
#include <errno.h>
#ifdef HAVE_ICONV
#include <iconv.h>
#endif
#ifdef WITH_LIBIDN2
#include <idn2.h>
#elif WITH_LIBIDN
#include <idna.h>
#endif

#include <libmget.h>
#include "private.h"

static const char
	*default_page = "index.html";
static size_t
	default_page_length = 10;

const char
	* const iri_schemes[] = { "http", "https", NULL },
	* const iri_ports[]   = { "80",   "443" };

#define IRI_CTYPE_GENDELIM (1<<0)
#define _iri_isgendelim(c) (iri_ctype[(unsigned char)(c)]&IRI_CTYPE_GENDELIM)

#define IRI_CTYPE_SUBDELIM (1<<1)
#define _iri_issubdelim(c) (iri_ctype[(unsigned char)(c)]&IRI_CTYPE_SUBDELIM)

#define IRI_CTYPE_UNRESERVED (1<<2)
#define _iri_isunreserved(c) (iri_ctype[(unsigned char)(c)]&IRI_CTYPE_UNRESERVED)

static const unsigned char
	iri_ctype[256] = {
		[':'] = IRI_CTYPE_GENDELIM,
		['/'] = IRI_CTYPE_GENDELIM,
		['?'] = IRI_CTYPE_GENDELIM,
		['#'] = IRI_CTYPE_GENDELIM,
		['['] = IRI_CTYPE_GENDELIM,
		[']'] = IRI_CTYPE_GENDELIM,
		['@'] = IRI_CTYPE_GENDELIM,

		['!'] = IRI_CTYPE_SUBDELIM,
		['$'] = IRI_CTYPE_SUBDELIM,
		['&'] = IRI_CTYPE_SUBDELIM,
//		['\\'] = IRI_CTYPE_SUBDELIM,
		['\''] = IRI_CTYPE_SUBDELIM,
		['('] = IRI_CTYPE_SUBDELIM,
		[')'] = IRI_CTYPE_SUBDELIM,
		['*'] = IRI_CTYPE_SUBDELIM,
		['+'] = IRI_CTYPE_SUBDELIM,
		[','] = IRI_CTYPE_SUBDELIM,
		[';'] = IRI_CTYPE_SUBDELIM,
		['='] = IRI_CTYPE_SUBDELIM,

		['-'] = IRI_CTYPE_UNRESERVED,
		['.'] = IRI_CTYPE_UNRESERVED,
		['_'] = IRI_CTYPE_UNRESERVED,
		['~'] = IRI_CTYPE_UNRESERVED
	};

int mget_iri_supported(const MGET_IRI *iri)
{
	int it;

	for (it = 0; iri_schemes[it]; it++) {
		if (iri_schemes[it] == iri->scheme)
			return 1;
	}

	return 0;
}

int mget_iri_isgendelim(char c)
{
	// return strchr(":/?#[]@",c)!=NULL;
	return _iri_isgendelim(c);
}

int mget_iri_issubdelim(char c)
{
	// return strchr("!$&\'()*+,;=",c)!=NULL;
	return _iri_issubdelim(c);
}

int mget_iri_isreserved(char c)
{
	return mget_iri_isgendelim(c) || mget_iri_issubdelim(c);
}

int mget_iri_isunreserved(char c)
{
	return c > 32 && c < 127 && (isalnum(c) || _iri_isunreserved(c));
}

int mget_iri_isunreserved_path(char c)
{
	return c > 32 && c < 127 && (isalnum(c) || _iri_isunreserved(c) || c == '/');
}

// needed as helper for blacklist.c/blacklist_free()
void mget_iri_free_content(MGET_IRI *iri)
{
	if (iri) {
		if (iri->host_allocated)
			xfree(iri->host);
		xfree(iri->connection_part);
	}
}

void mget_iri_free(MGET_IRI **iri)
{
	if (iri && *iri) {
		mget_iri_free_content(*iri);
		xfree(*iri);
	}
}

static unsigned char G_GNUC_MGET_CONST _unhex(unsigned char c)
{
	return c <= '9' ? c - '0' : (c <= 'F' ? c - 'A' + 10 : c - 'a' + 10);
}

// return 1: unescape occurred, string changed
static int _unescape(unsigned char *src)
{
	int ret = 0;
	unsigned char *dst = src;

	while (*src) {
		if (*src == '%') {
			if (isxdigit(src[1]) && isxdigit(src[2])) {
				*dst++ = (_unhex(src[1]) << 4) | _unhex(src[2]);
				src += 3;
				ret = 1;
				continue;
			}
		}

		*dst++ = *src++;
	}
	*dst = 0;

	return ret;
}

char *mget_charset_transcode(const char *src, const char *src_encoding, const char *dst_encoding)
{
	if (!src)
		return NULL;

#ifdef HAVE_ICONV
	if (!src_encoding)
		src_encoding = "iso-8859-1"; // default character-set for most browsers
	if (!dst_encoding)
		dst_encoding = "iso-8859-1"; // default character-set for most browsers

	if (strcasecmp(src_encoding, dst_encoding)) {
		char *ret = NULL;

		iconv_t cd=iconv_open(dst_encoding, src_encoding);

		if (cd != (iconv_t)-1) {
			char *tmp = (char *)src; // iconv won't change where src points to, but changes tmp itself
			size_t tmp_len = strlen(src);
			size_t dst_len = tmp_len * 6, dst_len_tmp = dst_len;
			char *dst = xmalloc(dst_len + 1), *dst_tmp = dst;

			if (iconv(cd, &tmp, &tmp_len, &dst_tmp, &dst_len_tmp) != (size_t)-1) {
				ret = strndup(dst, dst_len - dst_len_tmp);
				debug_printf("converted '%s' (%s) -> '%s' (%s)\n", src, src_encoding, ret, dst_encoding);
			} else
				error_printf(_("Failed to convert '%s' string into '%s' (%d)\n"), src_encoding, dst_encoding, errno);

			xfree(dst);
			iconv_close(cd);
		} else
			error_printf(_("Failed to prepare encoding '%s' into '%s' (%d)\n"), src_encoding, dst_encoding, errno);

		return ret;
	}
#endif

	return strdup(src);
}

int mget_str_needs_encoding(const char *s)
{
	while (*s > 0) s++;

	return !!*s;
}

char *mget_str_to_utf8(const char *src, const char *encoding)
{
	return mget_charset_transcode(src, encoding, "utf-8");
}

char *mget_utf8_to_str(const char *src, const char *encoding)
{
	return mget_charset_transcode(src, "utf-8", encoding);
}

// URIs are assumed to be unescaped at this point

MGET_IRI *mget_iri_parse(const char *url, const char *encoding)
{
	MGET_IRI *iri;
	const char *default_port = NULL;
	char *p, *s, *authority, c;
	size_t slen, it;
	int url_allocated;

	if (!url)
		return NULL;

	/*
		URI         = scheme ":" hier-part [ "?" query ] [ "#" fragment ]
		hier-part   = "//" authority path-abempty / path-absolute / path-rootless / path-empty
		scheme      =  ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
	 */
	while (isspace(*url)) url++;
	if (!*url) return NULL;

	// first unescape, than convert to UTF-8
	if (strchr(url, '%')) {
		char *unesc_url = strdup(url);

		_unescape((unsigned char *)unesc_url);

		if (mget_str_needs_encoding(unesc_url)) {
			if ((url = mget_str_to_utf8(unesc_url, encoding)))
				xfree(unesc_url);
			else
				url = unesc_url; // on error, use what we have
		}
		url_allocated = 1;
	} else {
		url_allocated = 0;

		if (mget_str_needs_encoding(url)) {
			if ((s = mget_str_to_utf8(url, encoding))) {
				url = s;
				url_allocated = 1;
			}
		}
	}

	// just use one block of memory for all parsed URI parts
	slen = strlen(url);
	iri = xmalloc(sizeof(MGET_IRI) + slen * 2 + 2);
	memset(iri, 0, sizeof(MGET_IRI));
	strcpy(((char *)iri) + sizeof(MGET_IRI), url);
	iri->uri = ((char *)iri) + sizeof(MGET_IRI);
	s = ((char *)iri) + sizeof(MGET_IRI) + slen + 1;
	strcpy(s, url);
	if (url_allocated)
		xfree(url);

	p = s;
	while (*s && !_iri_isgendelim(*s))
		s++;

	if (*s == ':' && s[1]=='/') {
		// found a scheme
		*s++ = 0;

		// find the scheme in our static list of supported schemes
		// for later comparisons we compare pointers (avoiding strcasecmnp())
		iri->scheme = p;
		for (it = 0; iri_schemes[it]; it++) {
			if (!strcasecmp(iri_schemes[it], p)) {
				iri->scheme = iri_schemes[it];
				default_port = iri_ports[it];
				break;
			}
		}

		if (iri->scheme == p) {
			// convert scheme to lowercase
			for (; *p; p++)
				if (*p >= 'A' && *p <= 'Z')
					*p |= 0x20;
		}

	} else {
		iri->scheme = IRI_SCHEME_DEFAULT;
		default_port = iri_ports[0]; // port 80
		s = p; // rewind
	}

	// this is true for http, https, ftp, file
	if (s[0] == '/' && s[1] == '/')
		s += 2;

	// authority
	authority = s;
	while (*s && *s != '/' && *s != '?' && *s != '#')
		s++;
	c = *s;
	if (c) *s++ = 0;

	// left over: [path][?query][#fragment]
	if (c == '/') {
		iri->path = s;
		while (*s && *s != '?' && *s != '#')
			s++;
		c = *s;
		if (c) *s++ = 0;
	}

	if (c == '?') {
		iri->query = s;
		while (*s && *s != '#')
			s++;
		c = *s;
		if (c) *s++ = 0;
	}

	if (c == '#') {
		iri->fragment = s;
		s += strlen(s);
	}

	if (*s) {
		debug_printf("unparsed rest '%s'\n", s);
	}

	if (*authority) {
		s = authority;
		p = strchr(authority, '@');
		if (p) {
			iri->userinfo = s;
			*p = 0;
			s = p + 1;
		}
		if (*s == '[') {
			p = strrchr(s, ']');
			if (p) {
				iri->host = s + 1;
				*p = 0;
				s = p + 1;
			} else {
				// something is broken
				iri->host = s + 1;
				s += strlen(s);
			}
		} else {
			iri->host = s;
			while (*s && *s != ':')
				s++;
		}
		if (*s == ':') {
			if (s[1]) {
				if (!default_port || (strcmp(s + 1, default_port) && atoi(s + 1) != atoi(default_port)))
					iri->port = s + 1;
			}
		}
		*s = 0;
 	}

	iri->resolv_port = iri->port ? iri->port : default_port;

	// now unescape all components (not interested in display, userinfo, password)
	if (iri->host) {
		for (p = (char *)iri->host; *p; p++) {
			if (*p >= 'A' && *p <= 'Z') // isupper() also returns true for chars > 0x7f, the test is not EBCDIC compatible ;-)
				*p |= 0x20;
		}
#ifdef WITH_LIBIDN2
		if (mget_str_needs_encoding(iri->host)) {
			char *host_asc = NULL;
			int rc;

			if ((rc = idn2_lookup_u8((uint8_t *)iri->host, (uint8_t **)&host_asc, 0)) == IDN2_OK) {
				debug_printf("idn2 '%s' -> '%s'\n", iri->host, host_asc);
				iri->host = host_asc;
				iri->host_allocated = 1;
			} else
				error_printf(_("toASCII failed (%d): %s\n"), rc, idn2_strerror(rc));
		}
#elif WITH_LIBIDN
		if (mget_str_needs_encoding(iri->host)) {
			char *host_asc = NULL;
			int rc;

			if ((rc = idna_to_ascii_8z(iri->host, &host_asc, IDNA_USE_STD3_ASCII_RULES)) == IDNA_SUCCESS) {
				// debug_printf("toASCII '%s' -> '%s'\n", iri->host, host_asc);
				iri->host = host_asc;
				iri->host_allocated = 1;
			} else
				error_printf(_("toASCII failed (%d): %s\n"), rc, idna_strerror(rc));
		}
#endif
	}
	else {
		if (iri->scheme == IRI_SCHEME_HTTP || iri->scheme == IRI_SCHEME_HTTPS) {
			error_printf(_("Missing host/domain in URI '%s'\n"), iri->uri);
			mget_iri_free(&iri);
			return NULL;
		}
	}

	// info_printf("%s: path '%s'\n", iri->uri, iri->path);

	return iri;
}

static char *_iri_build_connection_part(MGET_IRI *iri)
{
	char *tag;
	size_t len;

	if (iri->port) {
		len = strlen(iri->scheme) + strlen(iri->host) + strlen(iri->port) + 4 + 1;
		tag = xmalloc(len);

		sprintf(tag, "%s://%s:%s", iri->scheme, iri->host, iri->port);
	} else {
		len = strlen(iri->scheme) + strlen(iri->host) + 3 + 1;
		tag = xmalloc(len);

		sprintf(tag, "%s://%s", iri->scheme, iri->host);
	}

	return tag;
}

const char *mget_iri_get_connection_part(MGET_IRI *iri)
{
	if (iri) {
		if (!iri->connection_part)
			iri->connection_part = _iri_build_connection_part(iri);

		return iri->connection_part;
	}

	return NULL;
}

// normalize /../ and remove /./

static size_t G_GNUC_MGET_NONNULL_ALL _normalize_path(char *path)
{
	char *p1 = path, *p2 = path;

	debug_printf("path %s ->\n", path);

	// skip ./ and ../ at the beginning of the path
	for (;;) {
		if (*p2 == '/')
			p2++;
		else if (*p2 == '.') {
			if (p2[1] == '/')
				p2 += 2;
			else if (p2[1] == '.') {
				if (p2[2] == '/')
					p2 += 3;
				else if (!p2[2])
					p2 += 2;
				else
					break;
			}
			else if (!p2[1])
				p2++;
			else
				break;
		} else
			break;
	}

	// normalize path but stop at query or fragment
	while (*p2 && *p2 != '?' && *p2 != '#') {
		if (*p2 == '/') {
			if (p2[1] == '.') {
				if (!strncmp(p2, "/../", 4)) {
					// go one level up
					p2 += 3;
					while (p1 > path && *--p1 != '/');
				} else if (!strcmp(p2, "/..")) {
					p2 += 3;
					while (p1 > path && *--p1 != '/');
					if (p1 > path) *p1++='/';
				} else if (!strncmp(p2, "/./", 3)) {
					p2 += 2;
				} else if (!strcmp(p2, "/.")) {
					p2 += 2;
					if (p1 > path) *p1++='/';
				} else
					*p1++ = *p2++;
			} else if (p1 == path)
				p2++; // avoid leading slash
			else if (p2[1] == '/')
				p2++; // double slash to single slash
			else
				*p1++ = *p2++;
		} else
			*p1++ = *p2++;
	}

	if (p1 != p2) {
		while (*p2)
			*p1++ = *p2++;

		*p1 = 0;
	}

	debug_printf("     %s\n", path);

	return p1 - path;
}

// create an absolute URI from a base + relative URI

//char *iri_relative_to_absolute(IRI *iri, const char *tag, const char *val, size_t len, char *dst, size_t dst_size)
const char *mget_iri_relative_to_abs(MGET_IRI *base, const char *val, size_t len, mget_buffer_t *buf)
{
	debug_printf("*url = %.*s\n", (int)len, val);

	if (*val == '/') {
		if (base) {
			char path[len + 1];

			strlcpy(path, val, len + 1);

			if (len >= 2 && val[1] == '/') {
				char *p;

				// absolute URI without scheme: //authority/path...
				if ((p = strchr(path + 2, '/')))
					_normalize_path(p + 1);

				mget_buffer_strcpy(buf, base->scheme);
				mget_buffer_strcat(buf, ":");
				mget_buffer_strcat(buf, path);
				debug_printf("*1 %s\n", buf->data);
			} else {
				// absolute path
				_normalize_path(path);

				mget_buffer_strcpy(buf, mget_iri_get_connection_part(base));
				mget_buffer_strcat(buf, "/");
				mget_buffer_strcat(buf, path);
				debug_printf("*2 %s\n", buf->data);
			}
		} else
			return NULL;
	} else {
		// see if URI begins with a scheme:
		if (memchr(val, ':', len)) {
			// absolute URI
			if (buf) {
				mget_buffer_memcpy(buf, val, len);
				debug_printf("*3 %s\n", buf->data);
			} else {
				debug_printf("*3 %s\n", val);
				return val;
			}
		} else if (base) {
			// relative path
			const char *lastsep = base->path ? strrchr(base->path, '/') : NULL;
			mget_buffer_strcpy(buf, mget_iri_get_connection_part(base));
			mget_buffer_strcat(buf, "/");

			size_t tmp_len = buf->length;

			if (lastsep)
				mget_buffer_memcat(buf, base->path, lastsep - base->path + 1);

			if (len)
				mget_buffer_memcat(buf, val, len);

			buf->length = _normalize_path(buf->data + tmp_len) + tmp_len;

			debug_printf("*4 %s %zu\n", buf->data, buf->length);
		} else if (val[len] == 0) {
			return val;
		} else
			return NULL;
	}

	return buf->data;
}

MGET_IRI *mget_iri_parse_base(MGET_IRI *base, const char *url, const char *encoding)
{
	MGET_IRI *iri;

	if (base) {
		mget_buffer_t buf;
		char sbuf[256];

		mget_buffer_init(&buf, sbuf, sizeof(sbuf));
		iri = mget_iri_parse(mget_iri_relative_to_abs(base, url, strlen(url), &buf), encoding);
		mget_buffer_deinit(&buf);
	} else {
		// no base: just check URL for being an absolute URI
		iri = mget_iri_parse(mget_iri_relative_to_abs(NULL, url, strlen(url), NULL), encoding);
	}

	return iri;
}

// RFC conform comparison as described in http://tools.ietf.org/html/rfc2616#section-3.2.3
int mget_iri_compare(MGET_IRI *iri1, MGET_IRI *iri2)
{
	int n;

//	info_printf("iri %p %p %s:%s %s:%s\n",iri1,iri2,iri1->scheme,iri1->port,iri2->scheme,iri2->port);

/*
	if (!iri1->path) {
//		if (iri2->path && strcmp(iri2->path, "/"))
		if (iri2->path)
			return -1;
	}
	else if (!iri2->path) {
//		if (iri1->path && strcmp(iri1->path, "/"))
		if (iri1->path)
			return 1;
	}
*/
	if ((n = mget_strcasecmp(iri1->path, iri2->path)))
		return n;

	if ((n = mget_strcasecmp(iri1->query, iri2->query)))
		return n;

	if (iri1->scheme != iri2->scheme)
		return iri1->scheme < iri2->scheme ? -1 : 1;

	if (iri1->port != iri2->port)
		if ((n = mget_strcmp(iri1->port, iri2->port)))
			return n;

	// host is already lowercase, no need to call strcasecmp()
	if ((n = strcmp(iri1->host, iri2->host)))
		return n;

	// if ((n = null_strcasecmp(iri1->fragment, iri2->fragment)))
	//		return n;

	return 0;
}

const char *mget_iri_escape(const char *src, mget_buffer_t *buf)
{
	const char *begin;

	for (begin = src; *src; src++) {
		if (!mget_iri_isunreserved(*src)) {
			if (begin != src)
				mget_buffer_memcat(buf, begin, src - begin);
			begin = src + 1;
			mget_buffer_printf_append2(buf, "%%%02X", (unsigned char)*src);
		}
	}

	if (begin != src)
		mget_buffer_memcat(buf, begin, src - begin);

	return buf->data;
}

const char *mget_iri_escape_path(const char *src, mget_buffer_t *buf)
{
	const char *begin;

	for (begin = src; *src; src++) {
		if (!mget_iri_isunreserved_path(*src)) {
			if (begin != src)
				mget_buffer_memcat(buf, begin, src - begin);
			begin = src + 1;
			mget_buffer_printf_append2(buf, "%%%02X", (unsigned char)*src);
		}
	}

	if (begin != src)
		mget_buffer_memcat(buf, begin, src - begin);

	return buf->data;
}

const char *mget_iri_escape_query(const char *src, mget_buffer_t *buf)
{
	const char *begin;

	for (begin = src; *src; src++) {
		if (!mget_iri_isunreserved(*src) && *src != '=') {
			if (begin != src)
				mget_buffer_memcat(buf, begin, src - begin);
			begin = src + 1;
			if (*src == ' ')
				mget_buffer_memcat(buf, "+", 1);
			else
				mget_buffer_printf_append2(buf, "%%%02X", (unsigned char)*src);
		}
	}

	if (begin != src)
		mget_buffer_memcat(buf, begin, src - begin);

	return buf->data;
}

const char *mget_iri_get_escaped_host(const MGET_IRI *iri, mget_buffer_t *buf)
{
	return mget_iri_escape(iri->host, buf);
}

const char *mget_iri_get_escaped_resource(const MGET_IRI *iri, mget_buffer_t *buf)
{
	if (iri->path)
		mget_iri_escape_path(iri->path, buf);

	if (iri->query) {
		mget_buffer_memcat(buf, "?", 1);
		mget_iri_escape_query(iri->query, buf);
	}

	if (iri->fragment) {
		mget_buffer_memcat(buf, "#", 1);
		mget_iri_escape(iri->fragment, buf);
	}

	return buf->data;
}

char *mget_iri_get_path(const MGET_IRI *iri, mget_buffer_t *buf, const char *encoding)
{
	if (buf->length)
		mget_buffer_memcat(buf, "/", 1);

	if (iri->path) {
		if (mget_strcasecmp(encoding, "utf-8")) {
			char *fname = mget_utf8_to_str(iri->path, encoding);
			if (fname) {
				mget_buffer_strcat(buf, fname);
				xfree(fname);
			}
		} else {
			mget_buffer_strcat(buf, iri->path);
		}
	}

	if ((buf->length == 0 || buf->data[buf->length - 1] == '/') && default_page)
		mget_buffer_memcat(buf, default_page, default_page_length);

	return buf->data;
}

char *mget_iri_get_query_as_filename(const MGET_IRI *iri, mget_buffer_t *buf, const char *encoding)
{
	if (iri->query) {
		const char *query;
		int allocated = 0;

		mget_buffer_memcat(buf, "?", 1);

		if (mget_strcasecmp(encoding, "utf-8")) {
			if ((query = mget_utf8_to_str(iri->query, encoding)))
				allocated = 1;
		} else {
			query = iri->query;
		}

		if (query) {
			int slashes = 0;
			const char *src = query;

			// count slashes in query string
			while ((src = strchr(src, '/'))) {
				slashes++;
				src++;
			}

			if (slashes) {
				// escape slashes to use query as part of a filename
				const char *begin;

				for (src = begin = query; *src; src++) {
					if (*src == '/') {
						if (begin != src)
							mget_buffer_memcat(buf, begin, src - begin);
						begin = src + 1;
						mget_buffer_memcat(buf, "%2F", 3);
					}
				}

				if (begin != src)
					mget_buffer_memcat(buf, begin, src - begin);
			}

			mget_buffer_strcat(buf, query);
		}

		if (allocated)
			xfree(query);
	}

	return buf->data;
}

char *mget_iri_get_filename(const MGET_IRI *iri, mget_buffer_t *buf, const char *encoding)
{
	if (iri->path) {
		char *fname;

		if (mget_strcasecmp(encoding, "utf-8")) {
			if ((fname = strrchr(iri->path, '/')))
				fname = mget_utf8_to_str(fname + 1, encoding);
			else
				fname = mget_utf8_to_str(iri->path, encoding);

			if (fname) {
				mget_buffer_strcat(buf, fname);
				xfree(fname);
			}
		} else {
			if ((fname = strrchr(iri->path, '/')))
				mget_buffer_strcat(buf, fname + 1);
			else
				mget_buffer_strcat(buf, iri->path);
		}
	}

	if ((buf->length == 0 || buf->data[buf->length - 1] == '/') && default_page)
		mget_buffer_memcat(buf, default_page, default_page_length);

	return mget_iri_get_query_as_filename(iri, buf, encoding);
}

// escaping: see http://tools.ietf.org/html/rfc2396#2 following (especially 2.4.2)
/*const char *iri_escape(const char *uri)
{
	int esc = 0;
	const char *p;

	for (p = uri; *p; p++) {
		if (*p == '%') {
			if ((isxdigit(p[1]) && isxdigit(p[2])) || p[1] == '%')
				return uri; // assume that URI is already escaped
			esc++;
		} else if ()
	}
}
*/

void mget_iri_set_defaultpage(const char *page)
{
	default_page = page;
	default_page_length = default_page ? strlen(default_page) : 0;
}
