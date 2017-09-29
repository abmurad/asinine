/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asinine/x509.h"
#include "internal/utils.h"

void
dump_name(const x509_name_t *name) {
	char buf[256];

	for (size_t i = 0; i < name->num; i++) {
		const x509_rdn_t *rdn = &name->rdns[i];

		if (asn1_oid_to_string(buf, sizeof(buf), &rdn->oid) >= sizeof(buf)) {
			printf("  %s...: ", buf);
		} else {
			printf("  %s: ", buf);
		}

		asinine_err_t err;
		if ((err = asn1_string(&rdn->value, buf, sizeof(buf))) != ASININE_OK) {
			printf("%s\n", asinine_strerror(err));
		} else {
			printf("%s\n", buf);
		}
	}
}

int
dump_certificates(const uint8_t *contents, size_t length) {
	x509_cert_t *cert = calloc(1, sizeof(x509_cert_t));
	if (cert == NULL) {
		fprintf(stderr, "Failed to allocate memory\n");
		return 1;
	}

	int res = 0;
	char buf[256];

	asn1_parser_t parser;
	asn1_init(&parser, contents, length);

	while (!asn1_end(&parser)) {
		asinine_err_t err;
		if ((err = x509_parse(&parser, cert)) != ASININE_OK) {
			fprintf(stderr, "Invalid certificate: %s\n", asinine_strerror(err));
			res = 1;
			goto exit;
		}

		printf("---\n");
		printf("Version: %d, Algo: %d\n", cert->version,
		    cert->signature_algorithm);

		if (asn1_time_to_string(buf, sizeof(buf), &cert->valid_from) >=
		    sizeof(buf)) {
			fprintf(stderr, "Couldn't format time\n");
			res = 1;
			goto exit;
		}
		printf("Valid from: %s", buf);

		if (asn1_time_to_string(buf, sizeof(buf), &cert->valid_to) >=
		    sizeof(buf)) {
			fprintf(stderr, "Couldn't format time\n");
			res = 1;
			goto exit;
		}
		printf(", to: %s\n", buf);

		printf("Issuer:\n");
		dump_name(&cert->issuer);

		printf("Subject:\n");
		dump_name(&cert->issuer);

		printf("Public key: %d\n", cert->pubkey.algorithm);
		switch (cert->pubkey.algorithm) {
		case X509_PUBKEY_RSA:
			printf("  Public exponent:\n");
			hexdump(cert->pubkey.key.rsa.e, cert->pubkey.key.rsa.e_num, 1);
			printf("  Modulus:\n");
			hexdump(cert->pubkey.key.rsa.n, cert->pubkey.key.rsa.n_num, 1);
			break;
		case X509_PUBKEY_ECDSA:
			printf("Point:\n");
			hexdump(cert->pubkey.key.ecdsa.point,
			    cert->pubkey.key.ecdsa.point_num, 1);
			break;
		default:
			printf("NOT IMPLEMENTED\n");
		}
	}

exit:
	free(cert);
	return res;
}

int
main(int argc, const char *argv[]) {
	if (argc < 2) {
		printf("%s [<file>|-]\n", argv[0]);
		return 1;
	}

	FILE *fd = stdin;
	if (strcmp(argv[1], "-") != 0) {
		fd = fopen(argv[1], "rb");
		if (fd == NULL) {
			fprintf(stderr, "Could not open source\n");
			return 1;
		}
	}

	size_t length;
	uint8_t *contents = load(fd, &length);
	fclose(fd);

	if (contents == NULL) {
		return 1;
	}

	int res = dump_certificates(contents, length);
	free(contents);
	return res;
}
