// SPDX-License-Identifier: GPL-2.0+
/*
 * RFC 3161 TimeStampResp parser and verifier.
 *
 * Two-pass ASN.1 decode:
 *   ts_decoder       – outer TimeStampResp / CMS SignedData envelope
 *   tstinfo_decoder  – inner TSTInfo (DER inside eContent OCTET STRING)
 *   signerinfo_decoder – CMS SignerInfo (DER inside signerInfos SET)
 *
 * All three passes share the same ts_parse_context so that fields are
 * populated in a single call to ts_parse_response().
 */

#include <common.h>
#include <digest.h>
#include <rtc.h>
#include <linux/asn1_decoder.h>
#include <linux/oid_registry.h>
#include <linux/err.h>
#include <crypto/public_key.h>
#include <crypto/ts.h>

#include "ts.asn1.h"
#include "tstinfo.asn1.h"
#include "signerinfo.asn1.h"

/* id-messageDigest: 1.2.840.113549.1.9.4 */
static const u8 OID_MESSAGE_DIGEST[] = {
	0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x09, 0x04
};

struct ts_parse_context {
	int                      pki_status;
	struct ts_info_t        *info;
	struct ts_signer_info_t *signer;   /* NULL → skip signerinfo decode */
};

/* ------------------------------------------------------------------ */
/* DER helpers                                                          */
/* ------------------------------------------------------------------ */

/*
 * Read the next TLV at *p/*rem.  Advances *p past the full TLV and
 * decrements *rem.  tag_out, val_out, vlen_out may each be NULL.
 */
static int der_next_tlv(const u8 **p, size_t *rem,
			u8 *tag_out, const u8 **val_out, size_t *vlen_out)
{
	const u8 *d = *p;
	size_t r = *rem;
	size_t vlen, hdr;
	int n, i;

	if (r < 2)
		return -EBADMSG;
	if (tag_out)
		*tag_out = d[0];
	vlen = d[1];
	hdr  = 2;
	if (vlen > 0x7f) {
		n = vlen - 0x80;
		if (n < 1 || n > 3 || r < (size_t)(2 + n))
			return -EBADMSG;
		hdr += n;
		vlen = 0;
		for (i = 0; i < n; i++)
			vlen = (vlen << 8) | d[2 + i];
	}
	if (hdr + vlen > r)
		return -EBADMSG;
	if (val_out)
		*val_out  = d + hdr;
	if (vlen_out)
		*vlen_out = vlen;
	*p   = d + hdr + vlen;
	*rem = r - hdr - vlen;
	return 0;
}

/* Write a minimal DER length encoding into buf; return bytes written. */
static size_t der_encode_length(u8 *buf, size_t len)
{
	if (len < 0x80) {
		buf[0] = (u8)len;
		return 1;
	}
	if (len < 0x100) {
		buf[0] = 0x81;
		buf[1] = (u8)len;
		return 2;
	}
	buf[0] = 0x82;
	buf[1] = (u8)(len >> 8);
	buf[2] = (u8)(len & 0xff);
	return 3;
}

/* Map a DER-encoded OID to barebox enum hash_algo. */
static enum hash_algo oid_to_hash_algo(const struct asn1_buf_t *oid)
{
	switch (look_up_OID(oid->data, oid->len)) {
	case OID_sha1:   return HASH_ALGO_SHA1;
	case OID_sha256: return HASH_ALGO_SHA256;
	case OID_sha384: return HASH_ALGO_SHA384;
	case OID_sha512: return HASH_ALGO_SHA512;
	default:         return HASH_ALGO__LAST;
	}
}

/* ------------------------------------------------------------------ */
/* Outer decoder callbacks (ts_decoder)                                */
/* ------------------------------------------------------------------ */

/*
 * Called after PKIStatusInfo SEQUENCE is consumed.
 * value/vlen = full SEQUENCE content; extract status from first INTEGER TLV.
 */
int tsa_status(void *ctx, size_t hdrlen, unsigned char tag,
	       const void *value, size_t vlen)
{
	struct ts_parse_context *context = ctx;
	const u8 *p = value;
	unsigned int int_len, i;
	int status;

	if (vlen < 3 || p[0] != 0x02)
		return -EBADMSG;
	int_len = p[1];
	if (int_len == 0 || int_len > 4 || (unsigned int)(int_len + 2) > vlen)
		return -EBADMSG;
	status = 0;
	for (i = 0; i < int_len; i++)
		status = (status << 8) | p[2 + i];
	context->pki_status = status;
	return 0;
}

int tsa_timestamp_token(void *ctx, size_t hdrlen, unsigned char tag,
			const void *value, size_t vlen)
{
	return 0;
}

int tsa_content_type(void *ctx, size_t hdrlen, unsigned char tag,
		     const void *value, size_t vlen)
{
	return 0;
}

int tsa_cms_version(void *ctx, size_t hdrlen, unsigned char tag,
		    const void *value, size_t vlen)
{
	return 0;
}

int tsa_cms_econtent_type(void *ctx, size_t hdrlen, unsigned char tag,
			  const void *value, size_t vlen)
{
	return 0;
}

/*
 * Receives the raw TSTInfo DER bytes (eContent OCTET STRING content).
 * Saves them for the messageDigest check, then runs the inner decoder.
 */
int tsa_cms_econtent(void *ctx, size_t hdrlen, unsigned char tag,
		     const void *value, size_t vlen)
{
	struct ts_parse_context *context = ctx;

	if (context->signer) {
		context->signer->raw_econtent.data = value;
		context->signer->raw_econtent.len  = vlen;
	}
	return asn1_ber_decoder(&tstinfo_decoder, ctx, value, vlen);
}

/*
 * Called with the full SignedData content.  Manually walks the DER to locate
 * the signerInfos SET (tag 0x31), then runs the signerinfo decoder.
 *
 * SignedData layout after version / digestAlgorithms / encapContentInfo:
 *   certificates  [0] IMPLICIT  (tag A0, optional)
 *   crls          [1] IMPLICIT  (tag A1, optional)
 *   signerInfos   SET OF        (tag 31, required)
 */
int tsa_cms_content(void *ctx, size_t hdrlen, unsigned char tag,
		    const void *value, size_t vlen)
{
	struct ts_parse_context *context = ctx;
	const u8 *p = value;
	size_t rem = vlen;
	const u8 *v;
	size_t vl;
	int i, ret;

	if (!context->signer)
		return 0;

	/* Skip version INTEGER, digestAlgorithms ANY, encapContentInfo SEQ */
	for (i = 0; i < 3; i++) {
		ret = der_next_tlv(&p, &rem, NULL, NULL, NULL);
		if (ret < 0)
			return ret;
	}

	/* Skip optional certificates [A0] and crls [A1] */
	while (rem >= 2 && (p[0] == 0xA0 || p[0] == 0xA1)) {
		ret = der_next_tlv(&p, &rem, NULL, NULL, NULL);
		if (ret < 0)
			return ret;
	}

	/* Now positioned at signerInfos SET (tag 0x31) */
	if (rem < 2 || p[0] != 0x31)
		return -EBADMSG;

	/* Get SET content (one or more SignerInfo SEQUENCEs; TSA has exactly one) */
	ret = der_next_tlv(&p, &rem, NULL, &v, &vl);
	if (ret < 0)
		return ret;

	/* Decode the first (and only) SignerInfo starting with SEQUENCE tag */
	if (vl < 2 || v[0] != 0x30)
		return -EBADMSG;

	return asn1_ber_decoder(&signerinfo_decoder, ctx, v, vl);
}

/* ------------------------------------------------------------------ */
/* Inner decoder callbacks (tstinfo_decoder)                           */
/* ------------------------------------------------------------------ */

int tsa_tstinfo_version(void *ctx, size_t hdrlen, unsigned char tag,
			const void *value, size_t vlen)
{
	struct ts_parse_context *context = ctx;
	const u8 *p = value;

	if (vlen != 1 || p[0] != 0x01)
		return -EBADMSG;
	context->info->version = 1;
	return 0;
}

int tsa_tstinfo_policy(void *ctx, size_t hdrlen, unsigned char tag,
		       const void *value, size_t vlen)
{
	struct ts_parse_context *context = ctx;

	context->info->policy.data = value;
	context->info->policy.len  = vlen;
	return 0;
}

int tsa_tstinfo_serial(void *ctx, size_t hdrlen, unsigned char tag,
		       const void *value, size_t vlen)
{
	struct ts_parse_context *context = ctx;

	context->info->serial.data = value;
	context->info->serial.len  = vlen;
	return 0;
}

int tsa_tstinfo_gentime(void *ctx, size_t hdrlen, unsigned char tag,
			const void *value, size_t vlen)
{
	struct ts_parse_context *context = ctx;
	size_t copy_len;

	if (vlen < 15)
		return -EBADMSG;
	copy_len = min(vlen, sizeof(context->info->gen_time) - 1);
	memcpy(context->info->gen_time, value, copy_len);
	context->info->gen_time[copy_len] = '\0';
	return 0;
}

int tsa_tstinfo_ordering(void *ctx, size_t hdrlen, unsigned char tag,
			 const void *value, size_t vlen)
{
	struct ts_parse_context *context = ctx;
	const u8 *p = value;

	if (vlen < 1)
		return -EBADMSG;
	context->info->ordering = (p[0] != 0x00) ? 1 : 0;
	return 0;
}

int tsa_tstinfo_nonce(void *ctx, size_t hdrlen, unsigned char tag,
		      const void *value, size_t vlen)
{
	struct ts_parse_context *context = ctx;
	const u8 *p = value;
	u64 nonce = 0;
	size_t i, start = 0;

	if (vlen > 0 && p[0] == 0x00)
		start = 1;
	for (i = start; i < vlen && (i - start) < 8; i++)
		nonce = (nonce << 8) | p[i];
	context->info->nonce = nonce;
	return 0;
}

int tsa_tstinfo_impr_algo(void *ctx, size_t hdrlen, unsigned char tag,
			  const void *value, size_t vlen)
{
	struct ts_parse_context *context = ctx;

	context->info->imprint.algorithm_oid.data = value;
	context->info->imprint.algorithm_oid.len  = vlen;
	return 0;
}

int tsa_tstinfo_impr_hashed_message(void *ctx, size_t hdrlen,
				    unsigned char tag,
				    const void *value, size_t vlen)
{
	struct ts_parse_context *context = ctx;

	context->info->imprint.hash.data = value;
	context->info->imprint.hash.len  = vlen;
	return 0;
}

/* ------------------------------------------------------------------ */
/* SignerInfo decoder callbacks (signerinfo_decoder)                   */
/* ------------------------------------------------------------------ */

int tsa_signer_digest_algo(void *ctx, size_t hdrlen, unsigned char tag,
			   const void *value, size_t vlen)
{
	struct ts_parse_context *context = ctx;

	if (!context->signer)
		return 0;
	context->signer->digest_algo_oid.data = value;
	context->signer->digest_algo_oid.len  = vlen;
	return 0;
}

/*
 * signedAttrs [0] IMPLICIT SET OF Attribute.
 * The callback receives the SET content (without the A0 tag+length).
 * For hashing, the caller re-encodes with tag 0x31.
 */
int tsa_signer_signed_attrs(void *ctx, size_t hdrlen, unsigned char tag,
			    const void *value, size_t vlen)
{
	struct ts_parse_context *context = ctx;

	if (!context->signer)
		return 0;
	context->signer->signed_attrs.data = value;
	context->signer->signed_attrs.len  = vlen;
	return 0;
}

int tsa_signer_sig_algo(void *ctx, size_t hdrlen, unsigned char tag,
			const void *value, size_t vlen)
{
	struct ts_parse_context *context = ctx;

	if (!context->signer)
		return 0;
	context->signer->sig_algo_oid.data = value;
	context->signer->sig_algo_oid.len  = vlen;
	return 0;
}

int tsa_signer_signature(void *ctx, size_t hdrlen, unsigned char tag,
			 const void *value, size_t vlen)
{
	struct ts_parse_context *context = ctx;

	if (!context->signer)
		return 0;
	context->signer->signature.data = value;
	context->signer->signature.len  = vlen;
	return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int ts_parse_response(const uint8_t *data, size_t datalen,
		      int *pki_status, struct ts_info_t *info,
		      struct ts_signer_info_t *signer)
{
	struct ts_parse_context ctx = {
		.info   = info,
		.signer = signer,
	};
	int ret;

	memset(info, 0, sizeof(*info));
	if (signer)
		memset(signer, 0, sizeof(*signer));

	ret = asn1_ber_decoder(&ts_decoder, &ctx, data, datalen);
	if (ret < 0)
		return ret;
	if (pki_status)
		*pki_status = ctx.pki_status;
	return 0;
}

int ts_info_verify(const struct ts_info_t *info,
		   const uint8_t *data, size_t datalen)
{
	enum hash_algo algo;
	struct digest *d;
	u8 hash[64];
	unsigned int hash_len;
	int ret;

	algo = oid_to_hash_algo(&info->imprint.algorithm_oid);
	if (algo == HASH_ALGO__LAST)
		return -ENOENT;

	d = digest_alloc_by_algo(algo);
	if (!d)
		return -ENOENT;

	hash_len = digest_length(d);
	digest_init(d);
	digest_update(d, data, datalen);
	digest_final(d, hash);
	digest_free(d);

	if (info->imprint.hash.len != hash_len)
		return -EBADMSG;

	return memcmp(hash, info->imprint.hash.data, hash_len) ? -EBADMSG : 0;
}

/*
 * Walk signedAttrs content (SET OF Attribute) looking for id-messageDigest.
 * Compares the embedded OCTET STRING value against expected_hash/hash_len.
 */
static int check_message_digest(const struct asn1_buf_t *sa,
				const u8 *expected_hash, size_t hash_len)
{
	const u8 *p = sa->data;
	size_t rem = sa->len;

	while (rem >= 2) {
		u8 attr_tag;
		const u8 *attr_val;
		size_t attr_len;
		const u8 *ap;
		size_t ar;
		u8 oid_tag;
		const u8 *oid_val;
		size_t oid_len;

		if (der_next_tlv(&p, &rem, &attr_tag, &attr_val, &attr_len) < 0)
			return -EBADMSG;
		if (attr_tag != 0x30)
			continue;

		ap = attr_val;
		ar = attr_len;

		/* attrType OID */
		if (der_next_tlv(&ap, &ar, &oid_tag, &oid_val, &oid_len) < 0)
			return -EBADMSG;
		if (oid_tag != 0x06)
			continue;

		if (oid_len != sizeof(OID_MESSAGE_DIGEST) ||
		    memcmp(oid_val, OID_MESSAGE_DIGEST, oid_len) != 0)
			continue;

		/* attrValues SET { OCTET STRING } */
		{
			u8 set_tag;
			const u8 *set_val;
			size_t set_len;
			u8 oct_tag;
			const u8 *oct_val;
			size_t oct_len;
			const u8 *sp;
			size_t sr;

			if (der_next_tlv(&ap, &ar, &set_tag, &set_val, &set_len) < 0)
				return -EBADMSG;
			if (set_tag != 0x31)
				return -EBADMSG;

			sp = set_val;
			sr = set_len;
			if (der_next_tlv(&sp, &sr, &oct_tag, &oct_val, &oct_len) < 0)
				return -EBADMSG;
			if (oct_tag != 0x04)
				return -EBADMSG;

			if (oct_len != hash_len)
				return -EBADMSG;
			return memcmp(oct_val, expected_hash, hash_len) ?
			       -EBADMSG : 0;
		}
	}

	return -ENOENT;
}

int ts_verify_cms_signature(const struct ts_signer_info_t *signer,
			    const char *key_name, const char *keyring)
{
	enum hash_algo algo;
	struct digest *d;
	u8 hash[64];
	unsigned int hash_len;
	const struct public_key *key;
	u8 set_hdr[4]; /* 0x31 + up to 3 length bytes */
	size_t set_hdr_len;
	int ret;

	if (!signer->signed_attrs.data || !signer->signature.data ||
	    !signer->raw_econtent.data)
		return -EINVAL;

	algo = oid_to_hash_algo(&signer->digest_algo_oid);
	if (algo == HASH_ALGO__LAST)
		return -ENOENT;

	/* Step 1: hash(TSTInfo bytes) and check signedAttrs.messageDigest */
	d = digest_alloc_by_algo(algo);
	if (!d)
		return -ENOENT;

	hash_len = digest_length(d);
	digest_init(d);
	digest_update(d, signer->raw_econtent.data, signer->raw_econtent.len);
	digest_final(d, hash);
	digest_free(d);

	ret = check_message_digest(&signer->signed_attrs, hash, hash_len);
	if (ret < 0)
		return ret;

	/* Step 2: hash(SET_TAG || DER_length || signedAttrs_content) */
	d = digest_alloc_by_algo(algo);
	if (!d)
		return -ENOENT;

	set_hdr[0]  = 0x31;  /* substitute SET tag for [0] IMPLICIT */
	set_hdr_len = 1 + der_encode_length(set_hdr + 1,
					    signer->signed_attrs.len);

	digest_init(d);
	digest_update(d, set_hdr, set_hdr_len);
	digest_update(d, signer->signed_attrs.data, signer->signed_attrs.len);
	digest_final(d, hash);
	digest_free(d);

	/* Step 3: RSA/ECDSA verify */
	key = public_key_get(key_name, keyring);
	if (!key)
		return -ENOKEY;

	return public_key_verify(key, signer->signature.data,
				 signer->signature.len, hash, algo);
}

uint64_t ts_info_get_time(const struct ts_info_t *info)
{
	const char *s = info->gen_time;
	unsigned int year, month, day, hour, min, sec;

	/* "YYYYMMDDHHmmssZ" — minimum 15 chars, digits only before 'Z' */
	if (info->gen_time[0] == '\0')
		return 0;

#define DIGIT2(p) ((unsigned int)((p)[0] - '0') * 10 + (unsigned int)((p)[1] - '0'))
	year  = (unsigned int)(DIGIT2(s) * 100 + DIGIT2(s + 2));
	month = DIGIT2(s + 4);
	day   = DIGIT2(s + 6);
	hour  = DIGIT2(s + 8);
	min   = DIGIT2(s + 10);
	sec   = DIGIT2(s + 12);
#undef DIGIT2

	return (uint64_t)mktime(year, month, day, hour, min, sec);
}
