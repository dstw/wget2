/*
 * Copyright(c) 2017 Free Software Foundation, Inc.
 *
 * This file is part of libwget.
 *
 * Libwget is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Libwget is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libwget.  If not, see <https://www.gnu.org/licenses/>.
 *
 *
 * Testing --stats-* options
 */

#include <config.h>

#include <stdlib.h> // exit()
#include <string.h> // strlen()
#include "libtest.h"

static const char *mainpage = "\
<html>\n\
<head>\n\
  <title>Main Page</title>\n\
</head>\n\
<body>\n\
  <p>\n\
    Don't care.\n\
  </p>\n\
</body>\n\
</html>\n";

int main(void)
{
	wget_test_url_t urls[]={
		{	.name = "/index.html", // "gnosis" in UTF-8 greek
			.code = "200 Dontcare",
			.body = mainpage,
			.headers = {
				"Content-Type: text/html",
			}
		}
	};

	// functions won't come back if an error occurs
	wget_test_start_server(
		WGET_TEST_RESPONSE_URLS, &urls, countof(urls),
		0);

	static const char *stats_options[] = {
		"--stats-dns",
		"--stats-ocsp",
		"--stats-server",
		"--stats-site",
		"--stats-tls",
	};

	static const char *stats_format[] = {
		"human",
		"json",
		"csv",
	};

	char options[128];

	for (unsigned it = 0; it < countof(stats_options); it++) {
		// test stats option without additional params
		wget_test(
			// WGET_TEST_KEEP_TMPFILES, 1,
			WGET_TEST_OPTIONS, stats_options[it],
			WGET_TEST_REQUEST_URL, urls[0].name + 1,
			WGET_TEST_EXPECTED_ERROR_CODE, 0,
			WGET_TEST_EXPECTED_FILES, &(wget_test_file_t []) {
				{ urls[0].name + 1, urls[0].body },
				{	NULL } },
			0);

		// test stats option without format
		snprintf(options, sizeof(options), "%s=-", stats_options[it]);
		wget_test(
			// WGET_TEST_KEEP_TMPFILES, 1,
			WGET_TEST_OPTIONS, options,
			WGET_TEST_REQUEST_URL, urls[0].name + 1,
			WGET_TEST_EXPECTED_ERROR_CODE, 0,
			WGET_TEST_EXPECTED_FILES, &(wget_test_file_t []) {
				{ urls[0].name + 1, urls[0].body },
				{	NULL } },
			0);

		for (unsigned it2 = 0; it2 < countof(stats_format); it2++) {
			// test stats option with format
			snprintf(options, sizeof(options), "%s=%s:-", stats_options[it], stats_format[it2]);
			wget_test(
				// WGET_TEST_KEEP_TMPFILES, 1,
				WGET_TEST_OPTIONS, options,
				WGET_TEST_REQUEST_URL, urls[0].name + 1,
				WGET_TEST_EXPECTED_ERROR_CODE, 0,
				WGET_TEST_EXPECTED_FILES, &(wget_test_file_t []) {
					{ urls[0].name + 1, urls[0].body },
					{	NULL } },
				0);
		}
	}

	exit(0);
}