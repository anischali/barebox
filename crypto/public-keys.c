// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt) "public-keys: " fmt

#include <common.h>
#include <crypto/public_key.h>
#include <crypto/rsa.h>
#include <crypto/ecdsa.h>
#include <linux/list.h>
#include <malloc.h>
#include <xfuncs.h>

LIST_HEAD(keyring_registry);

struct keyring *keyring_find(const char *name)
{
	struct keyring *kr;

	if (!name)
		return NULL;

	list_for_each_entry(kr, &keyring_registry, node) {
		if (!strcmp(kr->name, name))
			return kr;
	}
	return NULL;
}

struct keyring *keyring_create(const char *name)
{
	struct keyring *kr;

	if (!name || !*name)
		return ERR_PTR(-EINVAL);

	if (keyring_find(name))
		return ERR_PTR(-EEXIST);

	kr = xzalloc(sizeof(*kr));
	kr->name = xstrdup(name);
	INIT_LIST_HEAD(&kr->links);
	INIT_LIST_HEAD(&kr->node);
	list_add_tail(&kr->node, &keyring_registry);

	return kr;
}

int keyring_link_key(struct keyring *kr, const struct public_key *key)
{
	struct keyring_link *link;

	if (!kr || !key)
		return -EINVAL;

	link = xzalloc(sizeof(*link));
	link->type = KEYRING_LINK_KEY;
	link->key = key;
	list_add_tail(&link->node, &kr->links);

	return 0;
}

int keyring_unlink_key(struct keyring *kr, const struct public_key *key)
{
	struct keyring_link *link, *n;

	if (!kr || !key)
		return -EINVAL;

	list_for_each_entry_safe(link, n, &kr->links, node) {
		if (link->type == KEYRING_LINK_KEY && link->key == key) {
			list_del(&link->node);
			free(link);
			return 0;
		}
	}
	return -ENOENT;
}

static bool keyring_contains(const struct keyring *kr, const struct keyring *target)
{
	const struct keyring_link *link;

	if (kr == target)
		return true;

	list_for_each_entry(link, &kr->links, node) {
		if (link->type != KEYRING_LINK_KEYRING)
			continue;
		if (keyring_contains(link->keyring, target))
			return true;
	}
	return false;
}

int keyring_link_keyring(struct keyring *kr, const struct keyring *sub)
{
	struct keyring_link *link;

	if (!kr || !sub)
		return -EINVAL;

	/*
	 * Refuse to create a cycle: if sub already (transitively) contains kr
	 * — or sub is kr itself — adding sub as a child of kr would loop.
	 * Assuming the graph was cycle-free before, this check is enough to
	 * keep it that way.
	 */
	if (keyring_contains(sub, kr))
		return -ELOOP;

	link = xzalloc(sizeof(*link));
	link->type = KEYRING_LINK_KEYRING;
	link->keyring = sub;
	list_add_tail(&link->node, &kr->links);

	return 0;
}

int keyring_unlink_keyring(struct keyring *kr, const struct keyring *sub)
{
	struct keyring_link *link, *n;

	if (!kr || !sub)
		return -EINVAL;

	list_for_each_entry_safe(link, n, &kr->links, node) {
		if (link->type == KEYRING_LINK_KEYRING && link->keyring == sub) {
			list_del(&link->node);
			free(link);
			return 0;
		}
	}
	return -ENOENT;
}

const struct public_key *keyring_iter_next(struct keyring_iter *it,
					   const struct keyring *root)
{
	if (it->depth < 0) {
		if (!root)
			return NULL;
		it->depth = 0;
		it->stack[0] = root;
		it->cursor[0] = (struct list_head *)&root->links;
	}

	while (it->depth >= 0) {
		struct list_head *head = (struct list_head *)&it->stack[it->depth]->links;
		struct list_head *next = it->cursor[it->depth]->next;
		struct keyring_link *link;

		if (next == head) {
			it->depth--;
			continue;
		}

		it->cursor[it->depth] = next;
		link = list_entry(next, struct keyring_link, node);

		if (link->type == KEYRING_LINK_KEY)
			return link->key;

		if (it->depth + 1 >= KEYRING_MAX_DEPTH) {
			pr_warn("keyring nesting too deep, skipping %s\n",
				link->keyring->name);
			continue;
		}

		it->depth++;
		it->stack[it->depth] = link->keyring;
		it->cursor[it->depth] = (struct list_head *)&link->keyring->links;
	}

	return NULL;
}

const struct public_key *keyring_find_key(const struct keyring *kr,
					  const char *key_name_hint)
{
	const struct public_key *key;

	if (!kr || !key_name_hint)
		return ERR_PTR(-EINVAL);

	for_each_key_in_keyring(key, kr) {
		if (!key->key_name_hint)
			continue;
		if (!strcmp(key->key_name_hint, key_name_hint))
			return key;
	}

	return ERR_PTR(-ENOENT);
}

int public_key_add(const char *keyring, const struct public_key *key)
{
	struct keyring *kr;
	const struct public_key *conflict;

	if (!keyring || !*keyring)
		return -EINVAL;

	kr = keyring_find(keyring);
	if (!kr) {
		kr = keyring_create(keyring);
		if (IS_ERR(kr))
			return PTR_ERR(kr);
	}

	conflict = keyring_find_key(kr, key->key_name_hint);
	if (!IS_ERR(conflict)) {
		pr_warn("Cannot add key: key_name_hint %s already exists in keyring %s\n",
			key->key_name_hint, keyring);
		return -EEXIST;
	}

	return keyring_link_key(kr, key);
}

/*
 * CMS ECDSA signatures are DER-encoded SEQUENCE { INTEGER r, INTEGER s }.
 * ecdsa_verify() expects raw r||s (each coord_bytes wide, zero-padded).
 */
static int ecdsa_sig_der_to_raw(const uint8_t *sig, size_t sig_len,
				uint8_t *out, unsigned int coord_bytes)
{
	const uint8_t *p = sig;
	size_t rem = sig_len;
	size_t seq_len;
	int i;

	if (rem < 2 || *p++ != 0x30)
		return -EBADMSG;

	seq_len = *p++;
	rem -= 2;
	if (seq_len & 0x80) {
		int n = seq_len & 0x7f;
		if (n < 1 || n > 2 || rem < (size_t)n)
			return -EBADMSG;
		seq_len = 0;
		for (i = 0; i < n; i++)
			seq_len = (seq_len << 8) | *p++;
		rem -= n;
	}
	if (seq_len > rem)
		return -EBADMSG;
	/* bound the INTEGER parsing below to the SEQUENCE's declared content */
	rem = seq_len;

	for (i = 0; i < 2; i++) {
		size_t ilen;
		uint8_t *dst;

		if (rem < 2 || *p++ != 0x02)
			return -EBADMSG;
		ilen = *p++;
		rem -= 2;
		if (ilen > rem)
			return -EBADMSG;

		/* strip sign-extension byte */
		if (ilen > 0 && *p == 0x00) {
			p++; ilen--; rem--;
		}
		if (ilen > coord_bytes)
			return -EBADMSG;

		dst = out + i * coord_bytes;
		memset(dst, 0, coord_bytes);
		memcpy(dst + coord_bytes - ilen, p, ilen);
		p += ilen;
		rem -= ilen;
	}
	if (rem != 0)
		return -EBADMSG;
	return 0;
}

static unsigned int ecdsa_coord_bytes(const char *curve_name)
{
	if (!strcmp(curve_name, "prime256v1"))
		return 32;
	if (!strcmp(curve_name, "secp384r1"))
		return 48;
	return 0;
}

int public_key_verify(const struct public_key *key, const uint8_t *sig,
		      const uint32_t sig_len, const uint8_t *hash,
		      enum hash_algo algo)
{
	switch (key->type) {
	case PUBLIC_KEY_TYPE_RSA:
		return rsa_verify(key->rsa, sig, sig_len, hash, algo);
	case PUBLIC_KEY_TYPE_ECDSA: {
		uint8_t raw_sig[96]; /* fits P-256 (64) and P-384 (96) */
		unsigned int coord_bytes = ecdsa_coord_bytes(key->ecdsa->curve_name);
		int ret;

		if (!coord_bytes)
			return -ENOSYS;
		ret = ecdsa_sig_der_to_raw(sig, sig_len, raw_sig, coord_bytes);
		if (ret < 0)
			return -EBADMSG;
		return ecdsa_verify(key->ecdsa, raw_sig, coord_bytes * 2, hash);
	}
	}

	return -ENOKEY;
}

extern const struct public_key_record __public_keys_start[];
extern const struct public_key_record __public_keys_end[];

static int init_public_keys(void)
{
	const struct public_key_record *rec;
	int ret;

	for (rec = __public_keys_start; rec != __public_keys_end; rec++) {
		ret = public_key_add(rec->keyring, rec->key);
		if (ret)
			pr_warn("error while adding key %s to %s: %pe\n",
				rec->key->key_name_hint ?: "(noname)",
				rec->keyring, ERR_PTR(ret));
	}

	return 0;
}

device_initcall(init_public_keys);

#ifdef CONFIG_CRYPTO_BUILTIN_KEYS
#include "public-keys.h"
#endif
