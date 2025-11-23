#ifndef TEE_TK_H
#define TEE_TK_H

int trusted_tee_get_random(unsigned char *key, size_t key_len);
int trusted_tee_seal(uint8_t *data, uint8_t *blob, size_t *len);
int trusted_tee_unseal(uint8_t *blob, uint8_t *data, size_t *len);

#endif /* TEE_AVB_H */
