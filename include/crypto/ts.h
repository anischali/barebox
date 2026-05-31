/*
 * Copyright (c) Conception Ro-Main.
 *
 * Anis Chali, Senior embedded designer, anis.chali@ro-main.com
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef _TS_H
#define _TS_H
#include <linux/types.h>

/* ---- ASN.1 / DER primitives ---- */
struct asn1_buf_t {
    const uint8_t *data;
    size_t len;
};

/* ---- SHA-256 / hash ---- */
typedef struct msg_imprint_t {
    struct asn1_buf_t algorithm_oid;   /* e.g. OID_SHA256 */
    struct asn1_buf_t hash;            /* OCTET STRING, 32 bytes for SHA-256 */
};

/* ---- TSTInfo (the signed payload) ---- */

struct ts_info_t {
    uint32_t                version;        /* always 1 */
    struct asn1_buf_t       policy;         /* TSA policy OID */
    struct msg_imprint_t    imprint;
    struct asn1_buf_t       serial;         /* INTEGER, big-endian bytes */
    char                    gen_time[32];   /* GeneralizedTime, "YYYYMMDDHHmmssZ" */
    int                     ordering;       /* BOOLEAN DEFAULT FALSE */
    uint64_t                nonce;          /* optional, 0 = absent */
    struct asn1_buf_t       tsa_name;       /* [0] GeneralName, optional */
};

int ts_info_verify(const uint8_t *data, size_t data_len);

#endif /* _TS_H */