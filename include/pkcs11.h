#ifndef PKCS11_H
#define PKCS11_H

#include <linux/list.h>

struct pkcs11_cryptoki_mech {
    u32 id;
    void *param;
    size_t param_len;
};

struct pkcs11_cryptoki_attr {
    u32 type;
    void *value;
    size_t value_len;
};

struct pkcs11_dev_info {
    char manufacturer[64];
    char library_description[128];
    u32 version_major;
    u32 version_minor;
};

struct pkcs11_cryptoki_ops {
    int (*init)(void);
    int (*finalize)(void);

    int (*open_session)(u32 slot_id, u32 flags, void **session);
    int (*close_session)(struct pksc11 *p);

    int (*login)(struct pksc11 *p, u32 user_type, const char *pin);
    int (*logout)(struct pksc11 *p);

    int (*generate_key)(struct pksc11 *p, const struct pkcs11_cryptoki_mech *mech,
                        const struct pkcs11_cryptoki_attr *attrs, size_t attr_count,
                        u32 *key_handle);

    int (*destroy_object)(struct pksc11 *p, u32 handle);

    int (*encrypt_init)(struct pksc11 *p, const struct pkcs11_cryptoki_mech *mech, u32 key_handle);
    int (*encrypt_update)(struct pksc11 *p, const void *in, size_t in_len, void *out, size_t *out_len);
    int (*encrypt_final)(struct pksc11 *p, void *out, size_t *out_len);

    int (*decrypt_init)(struct pksc11 *p, const struct pkcs11_cryptoki_mech *mech, u32 key_handle);
    int (*decrypt_update)(struct pksc11 *p, const void *in, size_t in_len, void *out, size_t *out_len);
    int (*decrypt_final)(struct pksc11 *p, void *out, size_t *out_len);

    int (*sign_init)(struct pksc11 *p, const struct pkcs11_cryptoki_mech *mech, u32 key_handle);
    int (*sign_update)(struct pksc11 *p, const void *data, size_t data_len);
    int (*sign_final)(struct pksc11 *p, void *sig, size_t *sig_len);

    int (*verify_init)(struct pksc11 *p, const struct pkcs11_cryptoki_mech *mech, u32 key_handle);
    int (*verify_update)(struct pksc11 *p, const void *data, size_t data_len);
    int (*verify_final)(struct pksc11 *p, const void *sig, size_t sig_len);

    int (*digest_init)(struct pksc11 *p, const struct pkcs11_cryptoki_mech *mech);
    int (*digest_update)(struct pksc11 *p, const void *data, size_t data_len);
    int (*digest_final)(struct pksc11 *p, void *hash, size_t *hash_len);

    int (*derive_key)(struct pksc11 *p, const struct pkcs11_cryptoki_mech *mech,
                      u32 base_key, const struct pkcs11_cryptoki_attr *attrs,
                      size_t attr_count, u32 *new_key);

    int (*get_info)(struct pkcs11_dev_info *info);
    int (*get_slot_list)(u32 *slot_list, size_t *slot_count);
    int (*get_mechanism_list)(u32 slot_id, u32 *mech_list, size_t *count);
};

struct pkcs11_cryptoki_info {
    const char *name;
    const char *driver;
};

struct pkcs11_cryptoki {
    struct pkcs11_cryptoki_info base;
    struct pkcs11_cryptoki_ops *ops;

    struct list_head list;
};


struct pkcs11 {

    const char *pin;
    u32 user_type;
    u32 slot_id;
    u32 flags;
    void *session;
    u32 *slot_list; 
    size_t slot_count;
    u32 *mech_list;
    size_t *mech_count;
    struct pkcs11_dev_info *info;
    struct pkcs11_cryptoki *cki;
};

#if defined(CONFIG_PKCS11)
int pkcs11_cryptoki_register(struct pkcs11_cryptoki *cki);
void pkcs11_cryptoki_unregister(struct pkcs11_cryptoki *cki);


#else
int pkcs11_cryptoki_register(struct pkcs11_cryptoki *cki)
{
    return 0;
}

void pkcs11_cryptoki_unregister(struct pkcs11_cryptoki *cki) {}

struct pkcs11 *pkcs11_allocate() {
    return NULL;
}

#include "pkcs11.h"

int pkcs11_open_session(struct pkcs11 *p) {
    return 0;
}

int pkcs11_close_session(struct pkcs11 *p) {
    return 0;
}

int pkcs11_login(struct pkcs11 *p) {
    return 0;
}

int pkcs11_logout(struct pkcs11 *p) {
    return 0;
}

int pkcs11_generate_key(struct pkcs11 *p, const struct pkcs11_cryptoki_mech *mech,
                        const struct pkcs11_cryptoki_attr *attrs, size_t attr_count,
                        u32 *key_handle) {
    return 0;
}

int pkcs11_destroy_object(struct pkcs11 *p, u32 handle) {
    return 0;
}

int pkcs11_encrypt_init(struct pkcs11 *p, const struct pkcs11_cryptoki_mech *mech, u32 key_handle) {
    return 0;
}

int pkcs11_encrypt_update(struct pkcs11 *p, const void *in, size_t in_len, void *out, size_t *out_len) {
    return 0;
}

int pkcs11_encrypt_final(struct pkcs11 *p, void *out, size_t *out_len) {
    return 0;
}

int pkcs11_decrypt_init(struct pkcs11 *p, const struct pkcs11_cryptoki_mech *mech, u32 key_handle) {
    return 0;
}

int pkcs11_decrypt_update(struct pksc11 *p, const void *in, size_t in_len, void *out, size_t *out_len) {
    return 0;
}

int pkcs11_decrypt_final(struct pksc11 *p, void *out, size_t *out_len) {
    return 0;
}

int pkcs11_sign_init(struct pksc11 *p, const struct pkcs11_cryptoki_mech *mech, u32 key_handle) {
    return 0;
}

int pkcs11_sign_update(struct pksc11 *p, const void *data, size_t data_len) {
    return 0;
}

int pkcs11_sign_final(struct pksc11 *p, void *sig, size_t *sig_len) {
    return 0;
}

int pkcs11_verify_init(struct pksc11 *p, const struct pkcs11_cryptoki_mech *mech, u32 key_handle) {
    return 0;
}

int pkcs11_verify_update(struct pksc11 *p, const void *data, size_t data_len) {
    return 0;
}

int pkcs11_verify_final(struct pksc11 *p, const void *sig, size_t sig_len) {
    return 0;
}

int pkcs11_digest_init(struct pksc11 *p, const struct pkcs11_cryptoki_mech *mech) {
    return 0;
}

int pkcs11_digest_update(struct pksc11 *p, const void *data, size_t data_len) {
    return 0;
}

int pkcs11_digest_final(struct pksc11 *p, void *hash, size_t *hash_len) {
    return 0;
}

int pkcs11_derive_key(struct pksc11 *p, const struct pkcs11_cryptoki_mech *mech,
                      u32 base_key, const struct pkcs11_cryptoki_attr *attrs,
                      size_t attr_count, u32 *new_key) {
    return 0;
}

int pkcs11_get_info(struct pkcs11_dev_info *info) {
    return 0;
}

int pkcs11_get_slot_list(u32 *slot_list, size_t *slot_count) {
    return 0;
}

int pkcs11_get_mechanism_list(u32 slot_id, u32 *mech_list, size_t *count) {
    return 0;
}

#endif



#endif