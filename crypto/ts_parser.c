// SPDX-License-Identifier: GPL-2.0+
/*
 * RFC 3161 TimeStampResp ASN.1 decoder.
 *
 * Three-pass decode sharing a single ts_parse_context:
 *   ts_decoder        – outer TimeStampResp / CMS SignedData envelope
 *   tstinfo_decoder   – inner TSTInfo (DER inside eContent OCTET STRING)
 *   signerinfo_decoder– CMS SignerInfo (DER inside signerInfos SET)
 *
 * Verification helpers live in ts_verify.c.
 */

#include <common.h>
#include <linux/asn1_decoder.h>
#include <linux/err.h>
#include <crypto/ts.h>

#include "ts_internal.h"
#include "ts.asn1.h"
#include "tstinfo.asn1.h"
#include "signerinfo.asn1.h"

/* ------------------------------------------------------------------ */
/* Outer decoder callbacks (ts_decoder)                                */
/* ------------------------------------------------------------------ */

/*
 * Called after PKIStatusInfo SEQUENCE is consumed (ASN1_OP_ACT after
 * ASN1_OP_END_SEQ).  value/vlen are the full SEQUENCE content; extract
 * the status INTEGER from the first TLV.
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
 * Receives the raw TSTInfo DER bytes from the eContent OCTET STRING.
 * Stores them for the messageDigest check then runs the inner decoder.
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
 * Called with the full SignedData SEQUENCE content.  Manually walks
 * the DER to locate the signerInfos SET (tag 0x31), skipping the three
 * required fields and any optional [A0]/[A1] fields, then runs the
 * signerinfo decoder on the SET content.
 *
 * SignedData after version / digestAlgorithms / encapContentInfo:
 *   certificates  [0] IMPLICIT  (tag 0xA0, optional)
 *   crls          [1] IMPLICIT  (tag 0xA1, optional)
 *   signerInfos   SET OF        (tag 0x31, required)
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

	/* Skip: version INTEGER, digestAlgorithms ANY, encapContentInfo SEQ */
	for (i = 0; i < 3; i++) {
		ret = der_next_tlv(&p, &rem, NULL, NULL, NULL);
		if (ret < 0)
			return ret;
	}

	/* Skip optional certificates [0xA0] and crls [0xA1] */
	while (rem >= 2 && (p[0] == 0xA0 || p[0] == 0xA1)) {
		ret = der_next_tlv(&p, &rem, NULL, NULL, NULL);
		if (ret < 0)
			return ret;
	}

	/* Positioned at signerInfos SET (tag 0x31) */
	if (rem < 2 || p[0] != 0x31)
		return -EBADMSG;

	ret = der_next_tlv(&p, &rem, NULL, &v, &vl);
	if (ret < 0)
		return ret;

	/* SET content is one SignerInfo SEQUENCE (exactly one for TSA) */
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

	/* Skip leading 0x00 positive-integer padding */
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
 * Callback receives the SET content bytes (A0 tag already consumed).
 * The SET tag must be substituted back (0x31) before hashing.
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
/* Public parse API                                                    */
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
