/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * RFC 3161 TimeStampResp parser and verifier.
 *
 * Copyright (c) Conception Ro-Main.
 * Anis Chali, Senior embedded designer, anis.chali@ro-main.com
 */

#ifndef _CRYPTO_TS_H
#define _CRYPTO_TS_H

#include <linux/types.h>

/* Raw pointer+length into the caller's DER buffer (zero-copy). */
struct asn1_buf_t {
	const uint8_t *data;
	size_t         len;
};

/* MessageImprint from TSTInfo. */
struct msg_imprint_t {
	struct asn1_buf_t algorithm_oid;  /* hash algorithm OID bytes */
	struct asn1_buf_t hash;           /* hashed message bytes */
};

/* Decoded TSTInfo fields.  All pointer fields reference into the original
 * DER buffer supplied to ts_parse_response(). */
struct ts_info_t {
	uint32_t           version;    /* always 1 */
	struct asn1_buf_t  policy;     /* TSA policy OID bytes */
	struct msg_imprint_t imprint;
	struct asn1_buf_t  serial;     /* serial number INTEGER bytes */
	char               gen_time[32]; /* "YYYYMMDDHHmmssZ" */
	int                ordering;   /* BOOLEAN DEFAULT FALSE */
	uint64_t           nonce;      /* 0 if absent */
	struct asn1_buf_t  tsa_name;   /* [0] GeneralName, optional */
};

/* Fields extracted from the CMS SignerInfo.
 * signed_attrs carries the raw content of the signedAttrs [0] field (the
 * SET content without its tag); callers must substitute tag 0x31 before
 * hashing, per RFC 5652 §5.4.
 * raw_econtent is the DER-encoded TSTInfo bytes from eContent. */
struct ts_signer_info_t {
	struct asn1_buf_t digest_algo_oid;  /* digestAlgorithm.algorithm */
	struct asn1_buf_t signed_attrs;     /* signedAttrs SET content (no A0 tag) */
	struct asn1_buf_t sig_algo_oid;     /* signatureAlgorithm.algorithm */
	struct asn1_buf_t signature;        /* signature OCTET STRING bytes */
	struct asn1_buf_t raw_econtent;     /* raw TSTInfo DER bytes */
};

/**
 * ts_parse_response - decode a DER-encoded RFC 3161 TimeStampResp
 * @data:       DER input buffer
 * @data_len:   length of @data
 * @pki_status: receives PKIStatus (0=granted, 1=grantedWithMods, ≥2=rejected)
 * @info:       receives decoded TSTInfo fields
 * @signer:     receives decoded SignerInfo fields, or NULL to skip
 *
 * All pointer fields in @info and @signer reference into @data; the buffer
 * must remain valid for as long as those structs are used.
 *
 * Returns 0 on success, -EBADMSG on malformed input.
 */
int ts_parse_response(const uint8_t *data, size_t data_len,
		      int *pki_status, struct ts_info_t *info,
		      struct ts_signer_info_t *signer);

/**
 * ts_info_verify - verify that @data matches the message imprint in @info
 * @info:     parsed TSTInfo (from ts_parse_response)
 * @data:     original content that was timestamped
 * @data_len: length of @data
 *
 * Hashes @data with the algorithm from info->imprint.algorithm_oid and
 * compares the result to info->imprint.hash.
 *
 * Returns 0 on match, -EBADMSG on mismatch, -ENOENT if the algorithm
 * is not available.
 */
int ts_info_verify(const struct ts_info_t *info,
		   const uint8_t *data, size_t data_len);

/**
 * ts_verify_cms_signature - verify the CMS signature over the timestamp token
 * @info:    parsed TSTInfo (from ts_parse_response)
 * @signer:  parsed SignerInfo (from ts_parse_response with non-NULL signer)
 * @keyring: keyring to search (e.g. "tsa")
 *
 * The key is looked up by converting info->policy (the TSA policy OID) to
 * its dotted-decimal string form (e.g. "1.3.6.1.4.1.4146.2.2") and calling
 * public_key_get() with that string.  Register TSA public keys in the
 * barebox keyring under the policy OID as the key name.
 *
 * Performs full RFC 5652 signature verification:
 *   1. signedAttrs.messageDigest == hash(raw TSTInfo bytes)
 *   2. RSA/ECDSA signature over hash(SET(signedAttrs))
 *
 * Returns 0 on success, -EKEYREJECTED on bad signature, -ENOKEY if the
 * key is not found, -EBADMSG on structural errors.
 */
int ts_verify_cms_signature(const struct ts_info_t *info,
			    const struct ts_signer_info_t *signer,
			    const char *keyring);

/**
 * ts_info_get_time - return the TSTInfo generation time as Unix seconds
 * @info: parsed TSTInfo
 *
 * Parses the gen_time GeneralizedTime string and converts it to seconds
 * since the Unix epoch (1970-01-01T00:00:00Z).
 *
 * Returns the timestamp on success, 0 if gen_time cannot be parsed.
 */
uint64_t ts_info_get_time(const struct ts_info_t *info);

#endif /* _CRYPTO_TS_H */
