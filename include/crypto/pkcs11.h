#ifndef PKCS11_H
#define PKCS11_H
#include <linux/list.h>


struct pkcs11;

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

struct pkcs11_cryptoki_info {
    char manufacturer[64];
    char library_description[128];
    u32 version_major;
    u32 version_minor;
};

struct pkcs11_cryptoki_ops {
    int (*init)(struct pkcs11 *p);
    int (*finalize)(struct pkcs11 *p);

    int (*open_session)(u32 slot_id, u32 flags, void **session);
    int (*close_session)(struct pkcs11 *p);

    int (*login)(struct pkcs11 *p, u32 user_type, const char *pin);
    int (*logout)(struct pkcs11 *p);

    int (*generate_key)(struct pkcs11 *p, const struct pkcs11_cryptoki_mech *mech,
                        const struct pkcs11_cryptoki_attr *attrs, size_t attr_count,
                        u32 *key_handle);

    int (*destroy_object)(struct pkcs11 *p, u32 handle);

    int (*encrypt_init)(struct pkcs11 *p, const struct pkcs11_cryptoki_mech *mech, u32 key_handle);
    int (*encrypt_update)(struct pkcs11 *p, const void *in, size_t in_len, void *out, size_t *out_len);
    int (*encrypt_final)(struct pkcs11 *p, void *out, size_t *out_len);

    int (*decrypt_init)(struct pkcs11 *p, const struct pkcs11_cryptoki_mech *mech, u32 key_handle);
    int (*decrypt_update)(struct pkcs11 *p, const void *in, size_t in_len, void *out, size_t *out_len);
    int (*decrypt_final)(struct pkcs11 *p, void *out, size_t *out_len);

    int (*sign_init)(struct pkcs11 *p, const struct pkcs11_cryptoki_mech *mech, u32 key_handle);
    int (*sign_update)(struct pkcs11 *p, const void *data, size_t data_len);
    int (*sign_final)(struct pkcs11 *p, void *sig, size_t *sig_len);

    int (*verify_init)(struct pkcs11 *p, const struct pkcs11_cryptoki_mech *mech, u32 key_handle);
    int (*verify_update)(struct pkcs11 *p, const void *data, size_t data_len);
    int (*verify_final)(struct pkcs11 *p, const void *sig, size_t sig_len);

    int (*digest_init)(struct pkcs11 *p, const struct pkcs11_cryptoki_mech *mech);
    int (*digest_update)(struct pkcs11 *p, const void *data, size_t data_len);
    int (*digest_final)(struct pkcs11 *p, void *hash, size_t *hash_len);

    int (*derive_key)(struct pkcs11 *p, const struct pkcs11_cryptoki_mech *mech,
                      u32 base_key, const struct pkcs11_cryptoki_attr *attrs,
                      size_t attr_count, u32 *new_key);

    int (*get_slot_list)(u32 *slot_list, size_t *slot_count);
    int (*get_mechanism_list)(u32 slot_id, u32 *mech_list, size_t *count);
};

struct pkcs11 {
    const char *name;
	
	struct list_head list;

	struct cdev cdev;
	struct device *dev;
	unsigned long priv;

    struct pkcs11_cryptoki_ops *ops;
};

#if defined(CONFIG_CRYPTO_PKCS11)
int pkcs11_register(struct device *dev, struct pkcs11 *cki);
void pkcs11_unregister(struct pkcs11 *cki);
struct pkcs11 *pkcs11_get_first(void);
#else
int pkcs11_register(struct device *dev, struct pkcs11 *cki)
{
    return 0;
}
void pkcs11_unregister(struct pkcs11 *cki) {}

struct pkcs11 *pkcs11_get_first(void)
{
    return NULL;
}

#endif



#endif