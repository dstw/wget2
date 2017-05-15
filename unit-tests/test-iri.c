/*
 * Copyright(c) 2017 Free Software Foundation, Inc.
 *
 * This file is part of Wget.
 *
 * Wget is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Wget is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Wget.  If not, see <https://www.gnu.org/licenses/>.
 *
 *
 * Testing IRIs
 *
 * Changelog
 * 10.09.2017  Didik Setiawan  created
 *
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <wget.h>
#include "../libwget/private.h"

#include "../src/wget_options.h"
#include "../src/wget_log.h"

static int
	ok,
	failed;

static void test_iri_path(void)
{
	const struct iri_test_data {
		const char
			*uri,
			*path,
			*charset;
	} test_data[] = {
		// test reserved character based on RFC 3987 section 2.2
		// gen-delims
		{ "http://example.com/foo:bar", "foo%3Abar", "utf-8" },
		// / should be passed through unchanged (for path)
		{ "http://example.com/foo/bar", "foo/bar", "utf-8" },
		// ? should be passed through unchanged (for query)
		{ "http://example.com/foo?bar", "foo?bar", "utf-8" },
		// hash is used to mark fragment
		//{ "http://example.com/foo#bar", "foo#bar", "utf-8" },
		{ "http://example.com/foo[bar", "foo%5Bbar", "utf-8" },
		{ "http://example.com/foo]bar", "foo%5Dbar", "utf-8" },
		// @ should be passed through unchanged (escaped or unescaped)
		{ "http://example.com/foo@bar", "foo@bar", "utf-8" },
		// sub-delims
		{ "http://example.com/foo!bar", "foo%21bar", "utf-8" },
		{ "http://example.com/foo$bar", "foo%24bar", "utf-8" },
		{ "http://example.com/foo&bar", "foo%26bar", "utf-8" },
		{ "http://example.com/foo'bar", "foo%27bar", "utf-8" },
		{ "http://example.com/foo(bar", "foo%28bar", "utf-8" },
		{ "http://example.com/foo)bar", "foo%29bar", "utf-8" },
		{ "http://example.com/foo*bar", "foo%2Abar", "utf-8" },
		{ "http://example.com/foo+bar", "foo%2Bbar", "utf-8" },
		{ "http://example.com/foo,bar", "foo%2Cbar", "utf-8" },
		{ "http://example.com/foo;bar", "foo%3Bbar", "utf-8" },
		{ "http://example.com/foo=bar", "foo%3Dbar", "utf-8" },
		// http://trac.webkit.org/browser/webkit/trunk/LayoutTests/fast/url/path.html
		// generic path definition
		{ "http://example.com/foo", "foo", "utf-8"},
		// valid escape sequence
		{ "http://example.com/%20foo", "%20foo", "utf-8"},
		// invalid escape sequence should pass through unchanged
		{ "http://example.com/foo%", "foo%", "utf-8"},
		{ "http://example.com/foo%2", "foo%2", "utf-8"},
		// invalid escape sequence: bad characters should be treated the same as
		// the sourrounding text, not as escaped (in this case, UTF-8)
		{ "http://example.com/foo%2zbar", "foo%252zbar", "utf-8"},
		{ "http://example.com/foo%2Â©zbar", "foo%2%C3%82%C2%A9zbar", "utf-8"},
		// regular characters that are escaped should be unescaped
		{ "http://example.com/foo%41%7a", "fooAz", "utf-8"},
		// invalid characters that are escaped should cause a failure
		// example: null char (%00) remove character behind
		{ "http://example.com/foo%00%51", "foo", "utf-8"},
		// some characters should be passed through unchanged regardless of esc
		{ "http://example.com/(%28:%3A%29)", "(%28:%3A%29)", "utf-8"},
		// characters that are properly escaped should not have the case changed
		// of hex letters.
		{ "http://example.com/%3A%3a%3C%3c", "%3A%3a%3C%3c", "utf-8"},
		// funny characters that are unescaped should be escaped
		{ "http://example.com/foo\tbar", "foobar", "utf-8"},
		// backslashes should get converted to forward slashes
		{ "http://example.com/foo\\\\bar", "foo/bar", "utf-8"},
		// hashes found in paths (possibly only when the caller explicitly sets
		// the path on an already-parsed URL) should be escaped
		// (disabled because requires ability to set path directly)
		//{ "http://example.com/foo#bar", "foo%23bar", "utf-8" },
		// %7f should be allowed and %3D should not be unescaped
		{ "http://example.com/%7Ffp3%3Eju%3Dduvgw%3Dd", "%7Ffp3%3Eju%3Dduvgw%3Dd", "utf-8"},
		// @ should be passed through unchanged (escaped or unescaped)
		{ "http://example.com/@asdf%40", "@asdf%40", "utf-8"},
		// basic conversion
		{ "http://example.com/你好你好", "%E4%BD%A0%E5%A5%BD%E4%BD%A0%E5%A5%BD", "utf-8"},
		// invalid unicode characters should fail. We only do validation on
		// UTF-16 input, so this doesn't happen on 8-bit.
		{ "http://example.com/﷐zyx", "%EF%B7%90zyx", "utf-8"},
		{ "http://example.com/\ufdd0zyx", "%EF%B7%90zyx", "utf-8"},
		// U+2025 TWO DOT LEADER should not be normalized to .. in the path
		{ "http://example.com/\u2025/foo", "%E2%80%A5/foo", "utf-8"},
		// BOM code point with special meaning U+FEFF ZERO WIDTH NO-BREAK SPACE
		{ "http://example.com/\uFEFF/foo", "%EF%BB%BF/foo", "utf-8"},
		// The BIDI override code points RLO and LRO
		{ "http://example.com/\u202E/foo/\u202D/bar",
		  "%E2%80%AE/foo/%E2%80%AD/bar", "utf-8"},
		// U+FF0F FULLWIDTH SOLIDUS
		{ "http://example.com/foo\uFF0Fbar", "foo%EF%BC%8Fbar", "utf-8"}
	};

	unsigned it;

	for (it = 0; it < countof(test_data); it++) {
		const struct iri_test_data *t = &test_data[it];
		wget_iri_t *iri = wget_iri_parse(t->uri, t->charset);
		wget_http_request_t *req = wget_http_create_request(iri, "GET");

		if (wget_strcmp(req->esc_resource.data, t->path)) {
			failed++;
			printf("IRI test #%u failed:\n", it + 1);
			printf(" [%s]\n", t->uri);
			printf(" result %s (expected %s)\n", req->esc_resource.data, t->path);
			printf("\n");
		} else
			ok++;

		wget_iri_free(&iri);
		wget_http_free_request(&req);
	}
}

static void test_iri_query(void)
{
	const struct iri_test_data {
		const char
			*uri,
			*query,
			*charset;
	} test_data[] = {
		// http://trac.webkit.org/browser/webkit/trunk/LayoutTests/fast/url/query.html
		// regular ASCII case in some different encodings
		{ "http://example.com/?foo=bar", "?foo=bar", "utf-8" },
		// allow question marks in the query without escaping
		{ "http://example.com/?as?df", "?as?df", "utf-8" },
		// Escape some questionable 8-bit characters, but never unescape
		{ "http://example.com/?%02hello%7f bye", "?%02hello%7f%20bye", "utf-8" },
		{ "http://example.com/?%40%41123", "?%40%41123", "utf-8" },
		// Chinese input/output
		{ "http://example.com/?q=\u4F60\u597D", "?q=%26%2320320%3B%26%2322909%3B", "utf-8" },
		// invalid UTF-8/16 input should be replaced with invalid characters
		{ "http://example.com/?q=\\ud800\\ud800",
		  "?q=%26%2355296%3B%26%2355296%3B", "utf-8" },
		// don't allow < or > because sometimes they are used for XSS if the
		// URL is echoed in content
		{ "http://example.com/?q=<asdf>", "?q=%3Casdf%3E", "utf-8" },
		// unescape double quotemarks in the query
		{ "http://example.com/?q=\"asdf\"", "?q=\"asdf\"", "utf-8" },
		// ';' should be unescape through query
		{ "http://example.com/?foo;bar", "?foo;bar", "utf-8" }
	};

	unsigned it;

	for (it = 0; it < countof(test_data); it++) {
		const struct iri_test_data *t = &test_data[it];
		wget_iri_t *iri = wget_iri_parse(t->uri, t->charset);
		wget_http_request_t *req = wget_http_create_request(iri, "GET");

		if (wget_strcmp(req->esc_resource.data, t->query)) {
			failed++;
			printf("IRI test #%u failed:\n", it + 1);
			printf(" [%s]\n", t->uri);
			printf(" result %s (expected %s)\n", req->esc_resource.data, t->query);
			printf("\n");
		} else
			ok++;

		wget_iri_free(&iri);
		wget_http_free_request(&req);
	}
}

static void test_iri_std_url(void)
{
	const struct iri_test_data {
		const char
			*uri,
			*path,
			*charset;
	} test_data[] = {
		// http://trac.webkit.org/browser/webkit/trunk/LayoutTests/fast/url/standard-url.html
		{ "http://example.com/foo?bar=baz#", "foo?bar=baz", "utf-8" },
		{ "http://example.com/foo%2Ehtml", "foo.html", "utf-8" }
	};

	unsigned it;

	for (it = 0; it < countof(test_data); it++) {
		const struct iri_test_data *t = &test_data[it];
		wget_iri_t *iri = wget_iri_parse(t->uri, t->charset);
		wget_http_request_t *req = wget_http_create_request(iri, "GET");

		if (wget_strcmp(req->esc_resource.data, t->path)) {
			failed++;
			printf("IRI test #%u failed:\n", it + 1);
			printf(" [%s]\n", t->uri);
			printf(" result %s (expected %s)\n", req->esc_resource.data, t->path);
			printf("\n");
		} else
			ok++;

		wget_iri_free(&iri);
		wget_http_free_request(&req);
	}
}

static void test_iri_whitespace(void)
{
	const struct iri_test_data {
		const char
			*uri,
			*query,
			*charset;
	} test_data[] = {
		// https://github.com/cweb/url-testing/blob/master/urls-local.json
		// subsection whitespace
		{ "http://example.com/ ", "%20", "utf-8" },
		{ "http://example.com/foo  bar/?  foo  =  bar  #  foo",
		  "foo%20%20bar/?%20%20foo%20%20=%20%20bar%20%20", "utf-8" }
	};

	unsigned it;

	for (it = 0; it < countof(test_data); it++) {
		const struct iri_test_data *t = &test_data[it];
		wget_iri_t *iri = wget_iri_parse(t->uri, t->charset);
		wget_http_request_t *req = wget_http_create_request(iri, "GET");

		if (wget_strcmp(req->esc_resource.data, t->query)) {
			failed++;
			printf("IRI test #%u failed:\n", it + 1);
			printf(" [%s]\n", t->uri);
			printf(" result %s (expected %s)\n", req->esc_resource.data, t->query);
			printf("\n");
		} else
			ok++;

		wget_iri_free(&iri);
		wget_http_free_request(&req);
	}
}

static void test_iri_percent_enc(void)
{
	const struct iri_test_data {
		const char
			*uri,
			*path,
			*charset;
	} test_data[] = {
		// https://github.com/cweb/url-testing/blob/master/urls-local.json
		// subsection percent-encoding
		{ "http://example.com/foo%3fbar", "foo?bar", "utf-8" },
		{ "http://example.com/foo%2fbar", "foo/bar", "utf-8" },
		{ "http://example.com/%A1%C1/?foo=%EF%BD%81", "%A1%C1/?foo=%EF%BD%81", "utf-8" },
		{ "http://example.com/%A1%C1/%EF%BD%81/?foo=%A1%C1", "%A1%C1/%EF%BD%81/?foo=%A1%C1", "utf-8" },
		{ "http://example.com/%A1%C1/?foo=???", "%A1%C1/?foo=???", "utf-8" },
		{ "http://example.com/\?\?\?/?foo=%A1%C1", "\?\?\?/?foo=%A1%C1", "utf-8" },
		{ "http://example.com/%A1%C1/?foo=???", "%A1%C1/?foo=???", "utf-8" },
		{ "http://example.com/\?\?\?/?foo=%A1%C1", "\?\?\?/?foo=%A1%C1", "utf-8" },
		{ "http://example.com/D%FCrst", "D%FCrst", "utf-8" },
		{ "http://example.com/D%C3%BCrst", "D%C3%BCrst", "utf-8" },
		{ "http://example.com/?D%FCrst", "?D%FCrst", "utf-8" },
		{ "http://example.com/?D%C3%BCrst", "?D%C3%BCrst", "utf-8" }
	};

	unsigned it;

	for (it = 0; it < countof(test_data); it++) {
		const struct iri_test_data *t = &test_data[it];
		wget_iri_t *iri = wget_iri_parse(t->uri, t->charset);
		wget_http_request_t *req = wget_http_create_request(iri, "GET");

		if (wget_strcmp(req->esc_resource.data, t->path)) {
			failed++;
			printf("IRI test #%u failed:\n", it + 1);
			printf(" [%s]\n", t->uri);
			printf(" result %s (expected %s)\n", req->esc_resource.data, t->path);
			printf("\n");
		} else
			ok++;

		wget_iri_free(&iri);
		wget_http_free_request(&req);
	}
}

int main(G_GNUC_WGET_UNUSED int argc, const char **argv)
{
	// if VALGRIND testing is enabled, we have to call ourselves with
	// valgrind checking
	const char *valgrind = getenv("VALGRIND_TESTS");

	if (!valgrind || !*valgrind || !strcmp(valgrind, "0")) {
		// fallthrough
	}
	else if (!strcmp(valgrind, "1")) {
		char cmd[strlen(argv[0]) + 256];

		snprintf(cmd, sizeof(cmd), "VALGRIND_TESTS=\"\" valgrind "
				"--error-exitcode=301 --leak-check=yes "
				"--show-reachable=yes --track-origins=yes %s",
				argv[0]);
		return system(cmd) != 0;
	} else {
		char cmd[strlen(valgrind) + strlen(argv[0]) + 32];

		snprintf(cmd, sizeof(cmd), "VALGRIND_TESTS="" %s %s",
				valgrind, argv[0]);
		return system(cmd) != 0;
	}

	test_iri_path();
	test_iri_query();
	test_iri_std_url();
	test_iri_whitespace();
	test_iri_percent_enc();

	//selftest_options() ? failed++ : ok++;

	if (failed) {
		printf("Summary: %d out of %d tests failed\n", failed, ok + failed);
		return 1;
	}

	printf("Summary: All %d tests passed\n", ok + failed);
	return 0;
}
