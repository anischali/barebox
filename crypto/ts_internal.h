/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Private header shared between ts_parser.c and ts_verify.c.
 * Not part of the public API.
 */
#ifndef _CRYPTO_TS_INTERNAL_H
#define _CRYPTO_TS_INTERNAL_H

#include <linux/types.h>
#include <digest.h>
#include <crypto/ts.h>

/* Context threaded through all three ASN.1 decoder passes. */
struct ts_parse_context {
	int                      pki_status;
	struct ts_info_t        *info;
	struct ts_signer_info_t *signer;  /* NULL → skip signerinfo pass */
};

/*
 * Read the next DER TLV at *p / *rem.  Advances *p past the full TLV and
 * decrements *rem accordingly.  tag_out, val_out, vlen_out may be NULL.
 * Returns 0 on success, -EBADMSG on malformed input.
 */
int der_next_tlv(const u8 **p, size_t *rem,
		 u8 *tag_out, const u8 **val_out, size_t *vlen_out);

/* Write a minimal DER length encoding into buf.  Returns bytes written. */
size_t der_encode_length(u8 *buf, size_t len);

/* Map a DER-encoded OID (bytes only, no tag/length) to enum hash_algo. */
enum hash_algo oid_to_hash_algo(const struct asn1_buf_t *oid);

#endif /* _CRYPTO_TS_INTERNAL_H */
