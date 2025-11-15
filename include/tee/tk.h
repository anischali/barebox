#ifndef TEE_TK_H
#define TEE_TK_H

int tk_get_random(u8 *buf, size_t size);
int tk_seal(u8 *in_buf, size_t in_size,
			      u8 *sealed, size_t *out_size);
int tk_unseal(u8 *in_buf, size_t in_size,
			      u8 *unsealed, size_t *out_size);

#endif /* TEE_AVB_H */
