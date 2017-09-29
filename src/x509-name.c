/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string.h>

#include "asinine/dsl.h"
#include "asinine/x509.h"

/**
 * Parse an X.509 Name.
 *
 * A Name is structured as follows:
 *
 *   SEQUENCE OF
 *     SET OF (one or more) (V3 with subjectAltName: zero) (= RDN)
 *       SEQUENCE (= AVA)
 *         OID Type
 *         ANY Value
 *
 * @param  parser Current position in the ASN.1 structure
 * @param  name Name structure to parse into
 * @return ASININE_OK on success, other error code otherwise.
 */
asinine_err_t
x509_parse_name(asn1_parser_t *parser, x509_name_t *name) {
	asinine_err_t err = x509_parse_optional_name(parser, name);
	if (err != ASININE_OK) {
		return err;
	}
	return name->num > 0 ? ASININE_OK : ASININE_ERR_INVALID;
}

/**
 * Parses an X.509 Name, which may be empty.
 */
asinine_err_t
x509_parse_optional_name(asn1_parser_t *parser, x509_name_t *name) {
	const asn1_token_t *token = &parser->token;

	*name = (x509_name_t){0};

	RETURN_ON_ERROR(asn1_push_seq(parser));

	// TODO: The sequence may be empty for V3 certificates, where the
	// subjectAltName extension is enabled.
	while (!asn1_eof(parser) && name->num < X509_MAX_RDNS) {
		// "RelativeDistinguishedName"
		NEXT_TOKEN(parser);

		if (!asn1_is_set(token)) {
			return ASININE_ERR_INVALID;
		}

		RETURN_ON_ERROR(asn1_push(parser));

		// "AttributeValueAssertion"
		RETURN_ON_ERROR(asn1_push_seq(parser));

		// Get identifiying key (OID)
		NEXT_TOKEN(parser);

		if (!asn1_is_oid(token)) {
			return ASININE_ERR_INVALID;
		}

		if (asn1_oid(token, &(name->rdns[name->num].oid)) != ASININE_OK) {
			return ASININE_ERR_INVALID;
		}

		// Get string value
		NEXT_TOKEN(parser);
		if (!asn1_is_string(token)) {
			return ASININE_ERR_INVALID;
		}

		name->rdns[name->num].value = *token;
		name->num++;

		// End of AVA
		RETURN_ON_ERROR(asn1_pop(parser));

		// TODO: Currently, only one AVA per RDN is supported
		if (!asn1_eof(parser)) {
			return ASININE_ERR_UNSUPPORTED_NAME;
		}

		// End of RDN
		RETURN_ON_ERROR(asn1_pop(parser));
	}

	if (!asn1_eof(parser)) {
		return ASININE_ERR_MEMORY;
	}

	x509_sort_name(name);

	return asn1_pop(parser);
}

void
x509_sort_name(x509_name_t *name) {
	for (size_t i = 1; i < name->num; i++) {
		x509_rdn_t temp = name->rdns[i];
		size_t j        = i;
		while (j > 0 && asn1_oid_cmp(&name->rdns[j - 1].oid, &temp.oid) > 0) {
			name->rdns[j] = name->rdns[j - 1];
			j--;
		}
		name->rdns[j] = temp;
	}
}

static void
set_reason(const char **ptr, const char *reason) {
	if (ptr == NULL) {
		return;
	}
	*ptr = reason;
}

bool
x509_name_eq(const x509_name_t *a, const x509_name_t *b, const char **reason) {
	if (a->num != b->num) {
		set_reason(reason, "differing number of RDNs");
		return false;
	}

	for (size_t i = 0; i < a->num; i++) {
		const x509_rdn_t *a_rdn = &a->rdns[i];
		const x509_rdn_t *b_rdn = &b->rdns[i];

		if (asn1_oid_cmp(&a_rdn->oid, &b_rdn->oid) != 0) {
			set_reason(reason, "attribute mismatch");
			return false;
		}

		if (a_rdn->value.length != b_rdn->value.length) {
			set_reason(reason, "value length mismatch");
			return false;
		}

		// TODO: This should compare normalised strings:
		//  - ignore case
		//  - decode from various charsets into a canonical one
		if (memcmp(a_rdn->value.data, b_rdn->value.data, a_rdn->value.length) !=
		    0) {
			set_reason(reason, "value mismatch");
			return false;
		}
	}

	set_reason(reason, NULL);
	return true;
}

asinine_err_t
x509_parse_alt_names(asn1_parser_t *parser, x509_alt_names_t *alt_names) {
	const asn1_token_t *token = &parser->token;

	*alt_names = (x509_alt_names_t){0};

	RETURN_ON_ERROR(asn1_push_seq(parser));

	// Alternative names must contain at least one name
	size_t i = 0;
	do {
		NEXT_TOKEN(parser);

		asn1_type_t type = token->type;
		if (type.class != ASN1_CLASS_CONTEXT) {
			return ASININE_ERR_INVALID;
		}

		switch ((uint8_t)type.tag) {
		case X509_ALT_NAME_RFC822NAME:
			if (token->length == 0) {
				return ASININE_ERR_INVALID;
			}
			break;
		case X509_ALT_NAME_DNSNAME:
			if (token->length == 0) {
				return ASININE_ERR_INVALID;
			}
			if (token->length == 1 && token->data[0] == ' ') {
				return ASININE_ERR_INVALID;
			}
			break;
		case X509_ALT_NAME_URI:
			if (token->length == 0) {
				return ASININE_ERR_INVALID;
			}
			// TODO: "The name
			//    MUST NOT be a relative URI, and it MUST follow the URI syntax
			//    and encoding rules specified in [RFC3986].  The name MUST
			//    include both a scheme (e.g., "http" or "ftp") and a
			//    scheme-specific-part.  URIs that include an authority
			//    ([RFC3986], Section 3.2) MUST include a fully qualified domain
			//    name or IP address as the host.
			//    As specified in [RFC3986], the scheme name is not
			//    case-sensitive (e.g., "http" is equivalent to "HTTP").  The
			//    host part, if present, is also not case-sensitive, but other
			//    components of the scheme-specific-part may be
			//    case-sensitive."
			break;
		case X509_ALT_NAME_IP:
			if (token->length != 4 && token->length != 16) {
				return ASININE_ERR_INVALID;
			}
			break;
		case 0: // otherName
		case 3: // x400Address
		case 4: // directoryAddress
		case 5: // ediPartyName
		case 8: // registeredID
			return ASININE_ERR_UNSUPPORTED_NAME;
			break;
		default:
			return ASININE_ERR_INVALID;
		}

		// At least directoryAddress uses constructed encoding, so we check
		// here to return UNSUPPORTED instead of INVALID.
		if (type.encoding != ASN1_ENCODING_PRIMITIVE) {
			return ASININE_ERR_INVALID;
		}

		alt_names->names[i].type   = (x509_alt_name_type_t)type.tag;
		alt_names->names[i].data   = token->data;
		alt_names->names[i].length = token->length;
		alt_names->num             = ++i;
	} while (!asn1_eof(parser) && i < X509_MAX_ALT_NAMES);

	if (!asn1_eof(parser)) {
		return ASININE_ERR_MEMORY;
	}

	return asn1_pop(parser);
}
