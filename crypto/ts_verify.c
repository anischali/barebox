// SPDX-License-Identifier: GPL-2.0+
/*
 * RFC 3161 / RFC 5652 timestamp verification.
 *
 * ts_info_verify    – verify message imprint (hash of original content)
 * ts_verify_cms_signature – verify CMS SignedData signature
 * ts_info_get_time  – decode GeneralizedTime to Unix seconds
 */

#include <common.h>
#include <digest.h>
#include <rtc.h>
#include <linux/oid_registry.h>
#include <linux/err.h>
#include <crypto/public_key.h>
#include <crypto/ts.h>

#include "ts_internal.h"

/* id-messageDigest: 1.2.840.113549.1.9.4 */
static const u8 OID_MESSAGE_DIGEST[] = {
	0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x09, 0x04
};

/* ------------------------------------------------------------------ */
/* DER helpers (also used by ts_parser.c via ts_internal.h)            */
/* ------------------------------------------------------------------ */

int der_next_tlv(const u8 **p, size_t *rem,
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
		n = (int)(vlen - 0x80);
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

size_t der_encode_length(u8 *buf, size_t len)
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

enum hash_algo oid_to_hash_algo(const struct asn1_buf_t *oid)
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
/* signedAttrs.messageDigest check                                     */
/* ------------------------------------------------------------------ */

/*
 * Walk signedAttrs content (SET OF Attribute, no outer tag) to find the
 * id-messageDigest attribute and compare its value to expected_hash.
 */
static int check_message_digest(const struct asn1_buf_t *sa,
				const u8 *expected_hash, size_t hash_len)
{
	const u8 *p = sa->data;
	size_t rem  = sa->len;

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
		if (attr_tag != 0x30)  /* Attribute SEQUENCE */
			continue;

		ap = attr_val;
		ar = attr_len;

		if (der_next_tlv(&ap, &ar, &oid_tag, &oid_val, &oid_len) < 0)
			return -EBADMSG;
		if (oid_tag != 0x06)
			continue;

		if (oid_len != sizeof(OID_MESSAGE_DIGEST) ||
		    memcmp(oid_val, OID_MESSAGE_DIGEST, oid_len) != 0)
			continue;

		/* Found id-messageDigest.  attrValues: SET { OCTET STRING } */
		{
			u8 set_tag;
			const u8 *set_val;
			size_t set_len;
			u8 oct_tag;
			const u8 *oct_val;
			size_t oct_len;
			const u8 *sp;
			size_t sr;

			if (der_next_tlv(&ap, &ar, &set_tag,
					 &set_val, &set_len) < 0)
				return -EBADMSG;
			if (set_tag != 0x31)
				return -EBADMSG;

			sp = set_val;
			sr = set_len;
			if (der_next_tlv(&sp, &sr, &oct_tag,
					 &oct_val, &oct_len) < 0)
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

/* ------------------------------------------------------------------ */
/* Public verification API                                             */
/* ------------------------------------------------------------------ */

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
	ret = digest_update(d, data, datalen);
	if (!ret)
		ret = digest_final(d, hash);
	digest_free(d);
	if (ret < 0)
		return ret;

	if (info->imprint.hash.len != hash_len)
		return -EBADMSG;

	return memcmp(hash, info->imprint.hash.data, hash_len) ? -EBADMSG : 0;
}

/*
 * Full RFC 5652 CMS signature verification:
 *
 *   1. Compute hash(TSTInfo bytes) and verify against
 *      signedAttrs.messageDigest so a substituted TSTInfo is caught.
 *   2. Compute hash(0x31 || DER_length || signedAttrs_content) — the
 *      SET tag substitution required by RFC 5652 §5.4.
 *   3. Look up the TSA key by converting info->policy OID to its
 *      dotted-decimal string (e.g. "1.3.6.1.4.1.4146.2.2") and calling
 *      public_key_get().  Register TSA keys under the policy OID string.
 */
int ts_verify_cms_signature(const struct ts_info_t *info,
			    const struct ts_signer_info_t *signer,
			    const char *keyring)
{
	char policy_oid_str[64];
	const struct public_key *key;
	enum hash_algo algo;
	struct digest *d;
	u8 hash[64];
	unsigned int hash_len;
	u8 set_hdr[4];
	size_t set_hdr_len;
	int ret;

	if (!signer->signed_attrs.data || !signer->signature.data ||
	    !signer->raw_econtent.data)
		return -EINVAL;

	/* Derive key name from TSTInfo policy OID */
	if (!info->policy.data || !info->policy.len)
		return -ENOKEY;
	ret = sprint_oid(info->policy.data, info->policy.len,
			 policy_oid_str, sizeof(policy_oid_str));
	if (ret < 0)
		return -ENOKEY;

	key = public_key_get(policy_oid_str, keyring);
	if (!key)
		return -ENOKEY;

	algo = oid_to_hash_algo(&signer->digest_algo_oid);
	if (algo == HASH_ALGO__LAST)
		return -ENOENT;

	/* Step 1: verify signedAttrs.messageDigest == hash(TSTInfo bytes) */
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

	/* Step 2: hash(SET_TAG || DER_length || signedAttrs content) */
	d = digest_alloc_by_algo(algo);
	if (!d)
		return -ENOENT;
	set_hdr[0]  = 0x31;   /* substitute SET tag for the [0] IMPLICIT */
	set_hdr_len = 1 + der_encode_length(set_hdr + 1,
					    signer->signed_attrs.len);
	digest_init(d);
	digest_update(d, set_hdr, set_hdr_len);
	digest_update(d, signer->signed_attrs.data, signer->signed_attrs.len);
	digest_final(d, hash);
	digest_free(d);

	/* Step 3: verify signature with the policy-keyed TSA key */
	return public_key_verify(key,
				 signer->signature.data,
				 signer->signature.len,
				 hash, algo);
}

uint64_t ts_info_get_time(const struct ts_info_t *info)
{
	const char *s = info->gen_time;
	unsigned int year, month, day, hour, min, sec;

#define D2(p) ((unsigned int)((p)[0] - '0') * 10 + (unsigned int)((p)[1] - '0'))
	if (s[0] == '\0')
		return 0;
	year  = D2(s) * 100 + D2(s + 2);
	month = D2(s + 4);
	day   = D2(s + 6);
	hour  = D2(s + 8);
	min   = D2(s + 10);
	sec   = D2(s + 12);
#undef D2

	return (uint64_t)mktime(year, month, day, hour, min, sec);
}
