/* btrfs.c - B-tree file system.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010,2011,2012,2013  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Tell zstd to expose functions that aren't part of the stable API, which
 * aren't safe to use when linking against a dynamic library. We vendor in a
 * specific zstd version, so we know what we're getting. We need these unstable
 * functions to provide our own allocator, which uses grub_malloc(), to zstd.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <common.h>
#include <driver.h>
#include <init.h>
#include <malloc.h>
#include <fs.h>
#include <command.h>
#include <linux/stat.h>
#include <linux/ctype.h>
#include <xfuncs.h>
#include <fcntl.h>
#include <common.h>
#include <malloc.h>
#include <errno.h>
#include <linux/time.h>
#include <asm/byteorder.h>
#include <errno.h>
#include "btrfs.h"

static phys_addr_t superblock_sectors[] = { 64 * 2, 64 * 1024 * 2,
					    256 * 1048576 * 2,
					    1048576ULL * 1048576ULL * 2 };

static int btrfs_read_logical(struct btrfs_data *data, phys_addr_t addr,
				void *buf, size_t size, int recursion_depth);

static int read_sblock(disk_t disk, struct btrfs_superblock *sb)
{
	struct btrfs_superblock sblock;
	unsigned i;
	int err = 0;
	for (i = 0; i < ARRAY_SIZE(superblock_sectors); i++) {
		/* Don't try additional superblocks beyond device size.  */
		if (i && (le_to_cpu64(sblock.this_device.size) >>
			  DISK_SECTOR_BITS) <= superblock_sectors[i])
			break;
		err = disk_read(disk, superblock_sectors[i], 0, sizeof(sblock),
				&sblock);
		if (err == -ERANGE)
			break;

		if (memcmp((char *)sblock.signature, BTRFS_SIGNATURE,
			   sizeof(BTRFS_SIGNATURE) - 1) != 0)
			break;
		if (i == 0 || le_to_cpu64(sblock.generation) >
				      le_to_cpu64(sb->generation))
			memcpy(sb, &sblock, sizeof(sblock));
	}

	if ((err == -ERANGE || !err) && i == 0) {
		return pr_err("not a Btrfs filesystem\n");
		return -EBADR;
	}

	if (err == -ERANGE)
		errno = err = 0;

	return err;
}

static int key_cmp(const struct btrfs_key *a, const struct btrfs_key *b)
{
	if (le_to_cpu64(a->object_id) < le_to_cpu64(b->object_id))
		return -1;
	if (le_to_cpu64(a->object_id) > le_to_cpu64(b->object_id))
		return +1;

	if (a->type < b->type)
		return -1;
	if (a->type > b->type)
		return +1;

	if (le_to_cpu64(a->offset) < le_to_cpu64(b->offset))
		return -1;
	if (le_to_cpu64(a->offset) > le_to_cpu64(b->offset))
		return +1;
	return 0;
}

static void free_iterator(struct btrfs_leaf_descriptor *desc)
{
	free(desc->data);
}

static int check_btrfs_header(struct btrfs_data *data,
				struct btrfs_header *header, phys_addr_t addr)
{
	if (le_to_cpu64(header->bytenr) != addr) {
		pr_err("btrfs_header.bytenr is not equal node addr\n");
		return -EBADR;
	}
	if (memcmp(data->sblock.uuid, header->uuid, sizeof(btrfs_uuid_t))) {
		pr_err("btrfs_header.uuid doesn't match sblock uuid");
		return -EBADR;
	}
	return 0;
}

static int save_ref(struct btrfs_leaf_descriptor *desc, phys_addr_t addr,
		      unsigned i, unsigned m, int l)
{
	desc->depth++;
	if (desc->allocated < desc->depth) {
		void *newdata;
		size_t sz;

		if (mul(desc->allocated, 2, &desc->allocated) ||
		    mul(desc->allocated, sizeof(desc->data[0]), &sz))
			return -ERANGE;

		newdata = realloc(desc->data, sz);
		if (!newdata)
			return errno;
		desc->data = newdata;
	}
	desc->data[desc->depth - 1].addr = addr;
	desc->data[desc->depth - 1].iter = i;
	desc->data[desc->depth - 1].maxiter = m;
	desc->data[desc->depth - 1].leaf = l;
	return 0;
}

static int next(struct btrfs_data *data, struct btrfs_leaf_descriptor *desc,
		phys_addr_t *outaddr, size_t *outsize,
		struct btrfs_key *key_out)
{
	int err;
	struct btrfs_leaf_node leaf;

	for (; desc->depth > 0; desc->depth--) {
		desc->data[desc->depth - 1].iter++;
		if (desc->data[desc->depth - 1].iter <
		    desc->data[desc->depth - 1].maxiter)
			break;
	}
	if (desc->depth == 0)
		return 0;
	while (!desc->data[desc->depth - 1].leaf) {
		struct btrfs_internal_node node;
		struct btrfs_header head;

		err = btrfs_read_logical(
			data,
			desc->data[desc->depth - 1].iter * sizeof(node) +
				sizeof(struct btrfs_header) +
				desc->data[desc->depth - 1].addr,
			&node, sizeof(node), 0);
		if (err)
			return -err;

		err = btrfs_read_logical(data, le_to_cpu64(node.addr), &head,
					 sizeof(head), 0);
		if (err)
			return -err;
		check_btrfs_header(data, &head, le_to_cpu64(node.addr));

		save_ref(desc, le_to_cpu64(node.addr), 0,
			 le_to_cpu32(head.nitems), !head.level);
	}
	err = btrfs_read_logical(data,
				 desc->data[desc->depth - 1].iter *
						 sizeof(leaf) +
					 sizeof(struct btrfs_header) +
					 desc->data[desc->depth - 1].addr,
				 &leaf, sizeof(leaf), 0);
	if (err)
		return -err;
	*outsize = le_to_cpu32(leaf.size);
	*outaddr = desc->data[desc->depth - 1].addr +
		   sizeof(struct btrfs_header) + le_to_cpu32(leaf.offset);
	*key_out = leaf.key;
	return 1;
}

static int lower_bound(struct btrfs_data *data,
			 const struct btrfs_key *key_in,
			 struct btrfs_key *key_out, uint64_t root,
			 phys_addr_t *outaddr, size_t *outsize,
			 struct btrfs_leaf_descriptor *desc,
			 int recursion_depth)
{
	phys_addr_t addr = le_to_cpu64(root);
	int depth = -1;

	if (desc) {
		desc->allocated = 16;
		desc->depth = 0;
		desc->data = calloc(desc->allocated, sizeof(desc->data[0]));
		if (!desc->data)
			return errno;
	}

	/* > 2 would work as well but be robust and allow a bit more just in case.
   */
	if (recursion_depth > 10) {
		pr_err("too deep btrfs virtual nesting\n");
		return -EBADR;
	}


	pr_info(
		"retrieving %" PRIxUINT64_T " %x %" PRIxUINT64_T "\n",
		key_in->object_id, key_in->type, key_in->offset);

	while (1) {
		int err;
		struct btrfs_header head;

reiter:
		depth++;
		/* FIXME: preread few nodes into buffer. */
		err = btrfs_read_logical(data, addr, &head, sizeof(head),
					 recursion_depth + 1);
		if (err)
			return err;
		check_btrfs_header(data, &head, addr);
		addr += sizeof(head);
		if (head.level) {
			unsigned i;
			struct btrfs_internal_node node, node_last;
			int have_last = 0;
			memset(&node_last, 0, sizeof(node_last));
			for (i = 0; i < le_to_cpu32(head.nitems); i++) {
				err = btrfs_read_logical(
					data, addr + i * sizeof(node), &node,
					sizeof(node), recursion_depth + 1);
				if (err)
					return err;

				pr_info(
					"internal node (depth %d) %" PRIxUINT64_T
					" %x %" PRIxUINT64_T "\n",
					depth, node.key.object_id,
					node.key.type, node.key.offset);

				if (key_cmp(&node.key, key_in) == 0) {
					err = 0;
					if (desc)
						err = save_ref(
							desc,
							addr - sizeof(head), i,
							le_to_cpu32(
								head.nitems),
							0);
					if (err)
						return err;
					addr = le_to_cpu64(node.addr);
					goto reiter;
				}
				if (key_cmp(&node.key, key_in) > 0)
					break;
				node_last = node;
				have_last = 1;
			}
			if (have_last) {
				err = 0;
				if (desc)
					err = save_ref(desc,
						       addr - sizeof(head),
						       i - 1,
						       le_to_cpu32(head.nitems),
						       0);
				if (err)
					return err;
				addr = le_to_cpu64(node_last.addr);
				goto reiter;
			}
			*outsize = 0;
			*outaddr = 0;
			memset(key_out, 0, sizeof(*key_out));
			if (desc)
				return save_ref(desc, addr - sizeof(head), -1,
						le_to_cpu32(head.nitems), 0);
			return 0;
		}
		{
			unsigned i;
			struct btrfs_leaf_node leaf, leaf_last;
			int have_last = 0;
			for (i = 0; i < le_to_cpu32(head.nitems); i++) {
				err = btrfs_read_logical(
					data, addr + i * sizeof(leaf), &leaf,
					sizeof(leaf), recursion_depth + 1);
				if (err)
					return err;

				pr_info(
					"leaf (depth %d) %" PRIxUINT64_T
					" %x %" PRIxUINT64_T "\n",
					depth, leaf.key.object_id,
					leaf.key.type, leaf.key.offset);

				if (key_cmp(&leaf.key, key_in) == 0) {
					memcpy(key_out, &leaf.key,
					       sizeof(*key_out));
					*outsize = le_to_cpu32(leaf.size);
					*outaddr =
						addr + le_to_cpu32(leaf.offset);
					if (desc)
						return save_ref(
							desc,
							addr - sizeof(head), i,
							le_to_cpu32(
								head.nitems),
							1);
					return 0;
				}

				if (key_cmp(&leaf.key, key_in) > 0)
					break;

				have_last = 1;
				leaf_last = leaf;
			}

			if (have_last) {
				memcpy(key_out, &leaf_last.key,
				       sizeof(*key_out));
				*outsize = le_to_cpu32(leaf_last.size);
				*outaddr = addr + le_to_cpu32(leaf_last.offset);
				if (desc)
					return save_ref(
						desc, addr - sizeof(head),
						i - 1, le_to_cpu32(head.nitems),
						1);
				return 0;
			}
			*outsize = 0;
			*outaddr = 0;
			memset(key_out, 0, sizeof(*key_out));
			if (desc)
				return save_ref(desc, addr - sizeof(head), -1,
						le_to_cpu32(head.nitems), 1);
			return 0;
		}
	}
}

/* Context for find_device.  */
struct find_device_ctx {
	struct btrfs_data *data;
	uint64_t id;
	struct device dev_found;
};

/* Helper for find_device.  */
static int find_device_iter(const char *name, void *data)
{
	struct find_device_ctx *ctx = data;
	struct device dev;
	int err;
	struct btrfs_superblock sb;

	dev = device_open(name);
	if (!dev)
		return 0;
	if (!dev->disk) {
		device_close(dev);
		return 0;
	}
	err = read_sblock(dev->disk, &sb);
	if (err == -EBADR) {
		device_close(dev);
		errno = 0;
		return 0;
	}
	if (err) {
		device_close(dev);
		print_error();
		return 0;
	}
	if (memcmp(ctx->data->sblock.uuid, sb.uuid, sizeof(sb.uuid)) != 0 ||
	    sb.this_device.device_id != ctx->id) {
		device_close(dev);
		return 0;
	}

	ctx->dev_found = dev;
	return 1;
}

static struct device find_device(struct btrfs_data *data, uint64_t id)
{
	struct find_device_ctx ctx = { .data = data,
				       .id = id,
				       .dev_found = NULL };
	unsigned i;

	for (i = 0; i < data->n_devices_attached; i++)
		if (id == data->devices_attached[i].id)
			return data->devices_attached[i].dev;

	device_iterate(find_device_iter, &ctx);

	data->n_devices_attached++;
	if (data->n_devices_attached > data->n_devices_allocated) {
		void *tmp;
		size_t sz;

		if (mul(data->n_devices_attached, 2,
			&data->n_devices_allocated) ||
		    add(data->n_devices_allocated, 1,
			&data->n_devices_allocated) ||
		    mul(data->n_devices_allocated,
			sizeof(data->devices_attached[0]), &sz))
			goto fail;

		data->devices_attached =
			realloc(tmp = data->devices_attached, sz);
		if (!data->devices_attached) {
			data->devices_attached = tmp;

fail:
			if (ctx.dev_found)
				device_close(ctx.dev_found);
			return NULL;
		}
	}
	data->devices_attached[data->n_devices_attached - 1].id = id;
	data->devices_attached[data->n_devices_attached - 1].dev =
		ctx.dev_found;
	return ctx.dev_found;
}

static int btrfs_read_from_chunk(struct btrfs_data *data,
				   struct btrfs_chunk_item *chunk,
				   uint64_t stripen, uint64_t stripe_offset,
				   int redundancy, uint64_t csize, void *buf)
{
	struct btrfs_chunk_stripe *stripe;
	phys_addr_t paddr;
	struct device dev;
	int err;

	stripe = (struct btrfs_chunk_stripe *)(chunk + 1);
	/* Right now the redundancy handling is easy.
       With RAID5-like it will be more difficult.  */
	stripe += stripen + redundancy;

	paddr = le_to_cpu64(stripe->offset) + stripe_offset;

	pr_info(
		"stripe %" PRIxUINT64_T " maps to 0x%" PRIxUINT64_T
		"\n"
		"reading paddr 0x%" PRIxUINT64_T "\n",
		stripen, stripe->offset, paddr);

	dev = find_device(data, stripe->device_id);
	if (!dev) {
		pr_info( "couldn't find a necessary member device "
				 "of multi-device filesystem\n");
		return -EINVAL;
	}

	err = disk_read(dev->disk, paddr >> DISK_SECTOR_BITS,
			paddr & (DISK_SECTOR_SIZE - 1), csize, buf);
	return err;
}

struct raid56_buffer {
	void *buf;
	int data_is_valid;
};

static void rebuild_raid5(char *dest, struct raid56_buffer *buffers,
			  uint64_t nstripes, uint64_t csize)
{
	uint64_t i;
	int first;

	for (i = 0; buffers[i].data_is_valid && i < nstripes; i++)
		;

	if (i == nstripes) {
		pr_info(
			"called rebuild_raid5(), but all disks are OK\n");
		return;
	}

	pr_info( "rebuilding RAID 5 stripe #%" PRIuUINT64_T "\n",
		i);

	for (i = 0, first = 1; i < nstripes; i++) {
		if (!buffers[i].data_is_valid)
			continue;

		if (first) {
			memcpy(dest, buffers[i].buf, csize);
			first = 0;
		} else
			crypto_xor(dest, dest, buffers[i].buf, csize);
	}
}

static int raid6_recover_read_buffer(void *data, int disk_nr,
				       uint64_t addr __attribute__((unused)),
				       void *dest, size_t size)
{
	struct raid56_buffer *buffers = data;

	if (!buffers[disk_nr].data_is_valid)
		return -EINVAL;

	memcpy(dest, buffers[disk_nr].buf, size);

	return errno = 0;
}

static void rebuild_raid6(struct raid56_buffer *buffers, uint64_t nstripes,
			  uint64_t csize, uint64_t parities_pos, void *dest,
			  uint64_t stripen)

{
	raid6_recover_gen(buffers, nstripes, stripen, parities_pos, dest, 0,
			  csize, 0, raid6_recover_read_buffer);
}

static int raid56_read_retry(struct btrfs_data *data,
			       struct btrfs_chunk_item *chunk,
			       uint64_t stripe_offset, uint64_t stripen,
			       uint64_t csize, void *buf, uint64_t parities_pos)
{
	struct raid56_buffer *buffers;
	uint64_t nstripes = le_to_cpu16(chunk->nstripes);
	uint64_t chunk_type = le_to_cpu64(chunk->type);
	int ret = ERR_OUT_OF_MEMORY;
	uint64_t i, failed_devices;

	buffers = calloc(nstripes, sizeof(*buffers));
	if (!buffers)
		goto cleanup;

	for (i = 0; i < nstripes; i++) {
		buffers[i].buf = zalloc(csize);
		if (!buffers[i].buf)
			goto cleanup;
	}

	for (failed_devices = 0, i = 0; i < nstripes; i++) {
		struct btrfs_chunk_stripe *stripe;
		phys_addr_t paddr;
		struct device dev;
		int err;

		/*
       * The struct btrfs_chunk_stripe array lives
       * behind struct btrfs_chunk_item.
       */
		stripe = (struct btrfs_chunk_stripe *)(chunk + 1) + i;

		paddr = le_to_cpu64(stripe->offset) + stripe_offset;
		pr_info(
			"reading paddr %" PRIxUINT64_T
			" from stripe ID %" PRIxUINT64_T "\n",
			paddr, stripe->device_id);

		dev = find_device(data, stripe->device_id);
		if (!dev) {
			pr_info(
				"stripe %" PRIuUINT64_T
				" FAILED (dev ID %" PRIxUINT64_T ")\n",
				i, stripe->device_id);
			failed_devices++;
			continue;
		}

		err = disk_read(dev->disk, paddr >> DISK_SECTOR_BITS,
				paddr & (DISK_SECTOR_SIZE - 1), csize,
				buffers[i].buf);
		if (err == 0) {
			buffers[i].data_is_valid = 1;
			pr_info(
				"stripe %" PRIuUINT64_T
				" OK (dev ID %" PRIxUINT64_T ")\n",
				i, stripe->device_id);
		} else {
			pr_info(
				"stripe %" PRIuUINT64_T
				" READ FAILED (dev ID %" PRIxUINT64_T
				")\n",
				i, stripe->device_id);
			failed_devices++;
		}
	}

	if (failed_devices > 1 && (chunk_type & BTRFS_CHUNK_TYPE_RAID5)) {
		pr_info(
			"not enough disks for RAID 5: total %" PRIuUINT64_T
			", missing %" PRIuUINT64_T "\n",
			nstripes, failed_devices);
		ret = ERR_READ_ERROR;
		goto cleanup;
	} else if (failed_devices > 2 &&
		   (chunk_type & BTRFS_CHUNK_TYPE_RAID6)) {
		pr_info(
			"not enough disks for RAID 6: total %" PRIuUINT64_T
			", missing %" PRIuUINT64_T "\n",
			nstripes, failed_devices);
		ret = ERR_READ_ERROR;
		goto cleanup;
	} else
		pr_info(
			"enough disks for RAID 5: total %" PRIuUINT64_T
			", missing %" PRIuUINT64_T "\n",
			nstripes, failed_devices);

	/* We have enough disks. So, rebuild the data. */
	if (chunk_type & BTRFS_CHUNK_TYPE_RAID5)
		rebuild_raid5(buf, buffers, nstripes, csize);
	else
		rebuild_raid6(buffers, nstripes, csize, parities_pos, buf,
			      stripen);

	ret = 0;
cleanup:
	if (buffers)
		for (i = 0; i < nstripes; i++)
			free(buffers[i].buf);
	free(buffers);

	return ret;
}

static int btrfs_read_logical(struct btrfs_data *data, phys_addr_t addr,
				void *buf, size_t size, int recursion_depth)
{
	while (size > 0) {
		uint8_t *ptr;
		struct btrfs_key *key;
		struct btrfs_chunk_item *chunk;
		uint64_t csize;
		int err = 0;
		struct btrfs_key key_out;
		int challoc = 0;
		struct btrfs_key key_in;
		size_t chsize;
		phys_addr_t chaddr;

		pr_info( "searching for laddr %" PRIxUINT64_T "\n",
			addr);
		for (ptr = data->sblock.bootstrap_mapping;
		     ptr < data->sblock.bootstrap_mapping +
				   sizeof(data->sblock.bootstrap_mapping) -
				   sizeof(struct btrfs_key);) {
			key = (struct btrfs_key *)ptr;
			if (key->type != BTRFS_ITEM_TYPE_CHUNK)
				break;
			chunk = (struct btrfs_chunk_item *)(key + 1);
			pr_info(
				"%" PRIxUINT64_T " %" PRIxUINT64_T
				" \n",
				le_to_cpu64(key->offset),
				le_to_cpu64(chunk->size));
			if (le_to_cpu64(key->offset) <= addr &&
			    addr < le_to_cpu64(key->offset) +
					    le_to_cpu64(chunk->size))
				goto chunk_found;
			ptr += sizeof(*key) + sizeof(*chunk) +
			       sizeof(struct btrfs_chunk_stripe) *
				       le_to_cpu16(chunk->nstripes);
		}

		key_in.object_id =
			cpu_to_le64_compile_time(BTRFS_OBJECT_ID_CHUNK);
		key_in.type = BTRFS_ITEM_TYPE_CHUNK;
		key_in.offset = cpu_to_le64(addr);
		err = lower_bound(data, &key_in, &key_out,
				  data->sblock.chunk_tree, &chaddr, &chsize,
				  NULL, recursion_depth);
		if (err)
			return err;
		key = &key_out;
		if (key->type != BTRFS_ITEM_TYPE_CHUNK ||
		    !(le_to_cpu64(key->offset) <= addr)) {
			pr_err("couldn't find the chunk descriptor\n");
			return -EBADR;
		}

		if (!chsize) {
			pr_err("got an invalid zero-size chunk\n");
			return -EBADR;
		}

		/*
       * The space being allocated for a chunk should at least be able to
       * contain one chunk item.
       */
		if (chsize < sizeof(struct btrfs_chunk_item)) {
			pr_err("got an invalid chunk size\n");
			return -EBADR;
		}
		chunk = malloc(chsize);
		if (!chunk)
			return errno;

		challoc = 1;
		err = btrfs_read_logical(data, chaddr, chunk, chsize,
					 recursion_depth);
		if (err) {
			free(chunk);
			return err;
		}

chunk_found : {
	uint64_t stripen;
	uint64_t stripe_offset;
	uint64_t off = addr - le_to_cpu64(key->offset);
	uint64_t chunk_stripe_length;
	uint16_t nstripes;
	unsigned redundancy = 1;
	unsigned i, j;
	int is_raid56;
	uint64_t parities_pos = 0;

	is_raid56 =
		!!(le_to_cpu64(chunk->type) &
		   (BTRFS_CHUNK_TYPE_RAID5 | BTRFS_CHUNK_TYPE_RAID6));

	if (le_to_cpu64(chunk->size) <= off) {
		pr_err("couldn't find the chunk descriptor\n");
		return -EBADR;
	}

	nstripes = le_to_cpu16(chunk->nstripes) ?: 1;
	chunk_stripe_length = le_to_cpu64(chunk->stripe_length) ?: 512;
	pr_info(
		"chunk 0x%" PRIxUINT64_T "+0x%" PRIxUINT64_T
		" (%d stripes (%d substripes) of %" PRIxUINT64_T ")\n",
		le_to_cpu64(key->offset), le_to_cpu64(chunk->size), nstripes,
		le_to_cpu16(chunk->nsubstripes), chunk_stripe_length);

	switch (le_to_cpu64(chunk->type) &
		~BTRFS_CHUNK_TYPE_BITS_DONTCARE) {
	case BTRFS_CHUNK_TYPE_SINGLE: {
		uint64_t stripe_length;
		pr_info( "single\n");
		stripe_length =
			divmod64(le_to_cpu64(chunk->size), nstripes, NULL);

		/* For single, there should be exactly 1 stripe. */
		if (le_to_cpu16(chunk->nstripes) != 1) {
			pr_err("invalid RAID_SINGLE: nstripes != 1 (%u)",
				     le_to_cpu16(chunk->nstripes));
			return -EBADR;
		}
		if (stripe_length == 0)
			stripe_length = 512;
		stripen = divmod64(off, stripe_length, &stripe_offset);
		csize = (stripen + 1) * stripe_length - off;
		break;
	}
	case BTRFS_CHUNK_TYPE_RAID1C4:
		redundancy++;
		/* fall through */
	case BTRFS_CHUNK_TYPE_RAID1C3:
		redundancy++;
		/* fall through */
	case BTRFS_CHUNK_TYPE_DUPLICATED:
	case BTRFS_CHUNK_TYPE_RAID1: {
		pr_info( "RAID1 (copies: %d)\n", ++redundancy);
		stripen = 0;
		stripe_offset = off;
		csize = le_to_cpu64(chunk->size) - off;

		/*
	      * Redundancy, and substripes only apply to RAID10, and there
	      * should be exactly 2 sub-stripes.
	      */
		if (le_to_cpu16(chunk->nstripes) != redundancy) {
			pr_err("invalid RAID1: nstripes != %u (%u)",
				     redundancy, le_to_cpu16(chunk->nstripes));
			return -EBADR;
		}
		break;
	}
	case BTRFS_CHUNK_TYPE_RAID0: {
		uint64_t middle, high;
		uint64_t low;
		pr_info( "RAID0\n");
		middle = divmod64(off, chunk_stripe_length, &low);

		high = divmod64(middle, nstripes, &stripen);
		stripe_offset = low + chunk_stripe_length * high;
		csize = chunk_stripe_length - low;
		break;
	}
	case BTRFS_CHUNK_TYPE_RAID10: {
		uint64_t middle, high;
		uint64_t low;
		uint16_t nsubstripes;
		nsubstripes = le_to_cpu16(chunk->nsubstripes) ?: 1;
		middle = divmod64(off, chunk_stripe_length, &low);

		high = divmod64(middle, nstripes / nsubstripes ?: 1, &stripen);
		stripen *= nsubstripes;
		redundancy = nsubstripes;
		stripe_offset = low + chunk_stripe_length * high;
		csize = chunk_stripe_length - low;

		/*
	       * Substripes only apply to RAID10, and there
	       * should be exactly 2 sub-stripes.
	       */
		if (le_to_cpu16(chunk->nsubstripes) != 2) {
			pr_err("invalid RAID10: nsubstripes != 2 (%u)",
				     le_to_cpu16(chunk->nsubstripes));
			return -EBADR;
		}

		break;
	}
	case BTRFS_CHUNK_TYPE_RAID5:
	case BTRFS_CHUNK_TYPE_RAID6: {
		uint64_t nparities, stripe_nr, high, low;

		redundancy = 1; /* no redundancy for now */

		if (le_to_cpu64(chunk->type) & BTRFS_CHUNK_TYPE_RAID5) {
			pr_info( "RAID5\n");
			nparities = 1;
		} else {
			pr_info( "RAID6\n");
			nparities = 2;
		}

		/*
	       * RAID 6 layout consists of several stripes spread over
	       * the disks, e.g.:
	       *
	       *   Disk_0  Disk_1  Disk_2  Disk_3
	       *     A0      B0      P0      Q0
	       *     Q1      A1      B1      P1
	       *     P2      Q2      A2      B2
	       *
	       * Note: placement of the parities depend on row number.
	       *
	       * Pay attention that the btrfs terminology may differ from
	       * terminology used in other RAID implementations, e.g. LVM,
	       * dm or md. The main difference is that btrfs calls contiguous
	       * block of data on a given disk, e.g. A0, stripe instead of chunk.
	       *
	       * The variables listed below have following meaning:
	       *   - stripe_nr is the stripe number excluding the parities
	       *     (A0 = 0, B0 = 1, A1 = 2, B1 = 3, etc.),
	       *   - high is the row number (0 for A0...Q0, 1 for Q1...P1, etc.),
	       *   - stripen is the disk number in a row (0 for A0, Q1, P2,
	       *     1 for B0, A1, Q2, etc.),
	       *   - off is the logical address to read,
	       *   - chunk_stripe_length is the size of a stripe (typically 64 KiB),
	       *   - nstripes is the number of disks in a row,
	       *   - low is the offset of the data inside a stripe,
	       *   - stripe_offset is the data offset in an array,
	       *   - csize is the "potential" data to read; it will be reduced
	       *     to size if the latter is smaller,
	       *   - nparities is the number of parities (1 for RAID 5, 2 for
	       *     RAID 6); used only in RAID 5/6 code.
	       */
		stripe_nr = divmod64(off, chunk_stripe_length, &low);

		/*
	       * stripen is computed without the parities
	       * (0 for A0, A1, A2, 1 for B0, B1, B2, etc.).
	       */
		if (nparities >= nstripes) {
			pr_err("invalid RAID5/6: nparities >= nstripes\n");
			return -EBADR;
		}
		high = divmod64(stripe_nr, nstripes - nparities, &stripen);

		/*
	       * The stripes are spread over the disks. Every each row their
	       * positions are shifted by 1 place. So, the real disks number
	       * change. Hence, we have to take into account current row number
	       * modulo nstripes (0 for A0, 1 for A1, 2 for A2, etc.).
	       */
		divmod64(high + stripen, nstripes, &stripen);

		/*
	       * parities_pos is equal to ((high - nparities) % nstripes)
	       * (see the diagram above). However, (high - nparities) can
	       * be negative, e.g. when high == 0, leading to an incorrect
	       * results. (high + nstripes - nparities) is always positive and
	       * modulo nstripes is equal to ((high - nparities) % nstripes).
	       */
		divmod64(high + nstripes - nparities, nstripes, &parities_pos);

		stripe_offset = chunk_stripe_length * high + low;
		csize = chunk_stripe_length - low;

		break;
	}
	default:
		pr_info("unsupported RAID flags %08x", le_to_cpu64(chunk->type));
		return -EOPNOTSUPP;
	}
	if (csize == 0) {
		pr_err("couldn't find the chunk descriptor\n");
		return -EINVAL;
	}
	if (csize > (uint64_t)size)
		csize = size;

	/*
	 * The space for a chunk stripe is limited to the space provide in the super-block's
	 * bootstrap mapping with an initial btrfs key at the start of each chunk.
	 */
	size_t avail_stripes =
		sizeof(data->sblock.bootstrap_mapping) /
		(sizeof(struct btrfs_key) + sizeof(struct btrfs_chunk_stripe));

	for (j = 0; j < 2; j++) {
		size_t est_chunk_alloc = 0;

		pr_info(
			"chunk 0x%" PRIxUINT64_T "+0x%" PRIxUINT64_T
			" (%d stripes (%d substripes) of %" PRIxUINT64_T
			")\n",
			le_to_cpu64(key->offset), le_to_cpu64(chunk->size),
			le_to_cpu16(chunk->nstripes),
			le_to_cpu16(chunk->nsubstripes),
			le_to_cpu64(chunk->stripe_length));
		pr_info( "reading laddr 0x%" PRIxUINT64_T "\n",
			addr);

		if (mul(sizeof(struct btrfs_chunk_stripe),
			le_to_cpu16(chunk->nstripes), &est_chunk_alloc) ||
		    add(est_chunk_alloc, sizeof(struct btrfs_chunk_item),
			&est_chunk_alloc) ||
		    est_chunk_alloc > chunk->size) {
			err = -EBADR;
			break;
		}

		if (le_to_cpu16(chunk->nstripes) > avail_stripes) {
			err = -EBADR;
			break;
		}

		if (is_raid56) {
			err = btrfs_read_from_chunk(data, chunk, stripen,
						    stripe_offset,
						    0, /* no mirror */
						    csize, buf);
			errno = 0;
			if (err)
				err = raid56_read_retry(data, chunk,
							stripe_offset, stripen,
							csize, buf,
							parities_pos);
		} else
			for (i = 0; i < redundancy; i++) {
				err = btrfs_read_from_chunk(data, chunk,
							    stripen,
							    stripe_offset,
							    i, /* redundancy */
							    csize, buf);
				if (!err)
					break;
				errno = 0;
			}
		if (!err)
			break;
	}
	if (err)
		return errno = err;
}
		size -= csize;
		buf = (uint8_t *)buf + csize;
		addr += csize;
		if (challoc)
			free(chunk);
	}
	return 0;
}

static struct btrfs_data *btrfs_mount(struct device dev)
{
	struct btrfs_data *data;
	int err;

	if (!dev->disk) {
		pr_err("not BtrFS\n");
		errno = -EBADR;
		return NULL;
	}

	data = zalloc(sizeof(*data));
	if (!data)
		return NULL;

	err = read_sblock(dev->disk, &data->sblock);
	if (err) {
		free(data);
		return NULL;
	}

	data->n_devices_allocated = 16;
	data->devices_attached = calloc(data->n_devices_allocated,
					sizeof(data->devices_attached[0]));
	if (!data->devices_attached) {
		free(data);
		return NULL;
	}
	data->n_devices_attached = 1;
	data->devices_attached[0].dev = dev;
	data->devices_attached[0].id = data->sblock.this_device.device_id;

	return data;
}

static void btrfs_unmount(struct btrfs_data *data)
{
	unsigned i;
	/* The device 0 is closed one layer upper.  */
	for (i = 1; i < data->n_devices_attached; i++)
		if (data->devices_attached[i].dev)
			device_close(data->devices_attached[i].dev);
	free(data->devices_attached);
	free(data->extent);
	free(data);
}

static int btrfs_read_inode(struct btrfs_data *data,
			      struct btrfs_inode *inode, uint64_t num,
			      uint64_t tree)
{
	struct btrfs_key key_in, key_out;
	phys_addr_t elemaddr;
	size_t elemsize;
	int err;

	key_in.object_id = num;
	key_in.type = BTRFS_ITEM_TYPE_INODE_ITEM;
	key_in.offset = 0;

	err = lower_bound(data, &key_in, &key_out, tree, &elemaddr, &elemsize,
			  NULL, 0);
	if (err)
		return err;
	if (num != key_out.object_id ||
	    key_out.type != BTRFS_ITEM_TYPE_INODE_ITEM) {
			pr_err("inode not found\n");
			return -EBADR;
		}

	return btrfs_read_logical(data, elemaddr, inode, sizeof(*inode), 0);
}

static void *zstd_malloc(void *state __attribute__((unused)), size_t size)
{
	return malloc(size);
}

static void zstd_free(void *state __attribute__((unused)), void *address)
{
	return free(address);
}

static ZSTD_customMem zstd_allocator(void)
{
	ZSTD_customMem allocator;

	allocator.customAlloc = &zstd_malloc;
	allocator.customFree = &zstd_free;
	allocator.opaque = NULL;

	return allocator;
}

static ssize_t btrfs_zstd_decompress(char *ibuf, size_t isize, off_t off,
				     char *obuf, size_t osize)
{
	void *allocated = NULL;
	char *otmpbuf = obuf;
	size_t otmpsize = osize;
	ZSTD_DCtx *dctx = NULL;
	size_t zstd_ret;
	ssize_t ret = -1;

	/*
   * Zstd will fail if it can't fit the entire output in the destination
   * buffer, so if osize isn't large enough, allocate a temporary buffer.
   */
	if (otmpsize < ZSTD_BTRFS_MAX_INPUT) {
		allocated = malloc(ZSTD_BTRFS_MAX_INPUT);
		if (!allocated) {
			ret = -ENOMEM;
			pr_err("failed allocate a zstd buffer\n");
			goto err;
		}
		otmpbuf = (char *)allocated;
		otmpsize = ZSTD_BTRFS_MAX_INPUT;
	}

	/* Create the ZSTD_DCtx. */
	dctx = ZSTD_createDCtx_advanced(zstd_allocator());
	if (!dctx) {
		/* ZSTD_createDCtx_advanced() only fails if it is out of memory. */
		pr_err("failed to create a zstd context\n");
		ret = -ENOMEM;
		goto err;
	}

	/*
   * Get the real input size, there may be junk at the
   * end of the frame.
   */
	isize = ZSTD_findFrameCompressedSize(ibuf, isize);
	if (ZSTD_isError(isize)) {
		ret = -EINVAL;
		pr_err("zstd failed to get size, data corrupted\n");
		goto err;
	}

	/* Decompress and check for errors. */
	zstd_ret = ZSTD_decompressDCtx(dctx, otmpbuf, otmpsize, ibuf, isize);
	if (ZSTD_isError(zstd_ret)) {
		pr_err("zstd failed to decompress, data corrupted\n");
		goto err;
	}

	/*
   * Move the requested data into the obuf. obuf may be equal
   * to otmpbuf, which is why memmove() is required.
   */
	memmove(obuf, otmpbuf + off, osize);
	ret = osize;

err:
	free(allocated);
	ZSTD_freeDCtx(dctx);

	return ret;
}

static ssize_t btrfs_lzo_decompress(char *ibuf, size_t isize, off_t off,
				    char *obuf, size_t osize)
{
	uint32_t total_size, cblock_size;
	size_t ret = 0;
	char *ibuf0 = ibuf;

	total_size = le_to_cpu32(get_unaligned32(ibuf));
	ibuf += sizeof(total_size);

	if (isize < total_size)
		return -1;

	/* Jump forward to first block with requested data.  */
	while (off >= BTRFS_LZO_BLOCK_SIZE) {
		/* Don't let following uint32_t cross the page boundary.  */
		if (((ibuf - ibuf0) & 0xffc) == 0xffc)
			ibuf = ((ibuf - ibuf0 + 3) & ~3) + ibuf0;

		cblock_size = le_to_cpu32(get_unaligned32(ibuf));
		ibuf += sizeof(cblock_size);

		if (cblock_size > BTRFS_LZO_BLOCK_MAX_CSIZE)
			return -1;

		off -= BTRFS_LZO_BLOCK_SIZE;
		ibuf += cblock_size;
	}

	while (osize > 0) {
		lzo_uint usize = BTRFS_LZO_BLOCK_SIZE;

		/* Don't let following uint32_t cross the page boundary.  */
		if (((ibuf - ibuf0) & 0xffc) == 0xffc)
			ibuf = ((ibuf - ibuf0 + 3) & ~3) + ibuf0;

		cblock_size = le_to_cpu32(get_unaligned32(ibuf));
		ibuf += sizeof(cblock_size);

		if (cblock_size > BTRFS_LZO_BLOCK_MAX_CSIZE)
			return -1;

		/* Block partially filled with requested data.  */
		if (off > 0 || osize < BTRFS_LZO_BLOCK_SIZE) {
			size_t to_copy = BTRFS_LZO_BLOCK_SIZE - off;
			uint8_t *buf;

			if (to_copy > osize)
				to_copy = osize;

			buf = malloc(BTRFS_LZO_BLOCK_SIZE);
			if (!buf)
				return -1;

			if (lzo1x_decompress_safe((lzo_bytep)ibuf, cblock_size,
						  buf, &usize,
						  NULL) != LZO_E_OK) {
				free(buf);
				return -1;
			}

			if (to_copy > usize)
				to_copy = usize;
			memcpy(obuf, buf + off, to_copy);

			osize -= to_copy;
			ret += to_copy;
			obuf += to_copy;
			ibuf += cblock_size;
			off = 0;

			free(buf);
			continue;
		}

		/* Decompress whole block directly to output buffer.  */
		if (lzo1x_decompress_safe((lzo_bytep)ibuf, cblock_size,
					  (lzo_bytep)obuf, &usize,
					  NULL) != LZO_E_OK)
			return -1;

		osize -= usize;
		ret += usize;
		obuf += usize;
		ibuf += cblock_size;
	}

	return ret;
}

static ssize_t btrfs_extent_read(struct btrfs_data *data, uint64_t ino,
				 uint64_t tree, off_t pos0, char *buf,
				 size_t len)
{
	off_t pos = pos0;
	while (len) {
		size_t csize;
		int err;
		off_t extoff;
		struct btrfs_leaf_descriptor desc;

		if (!data->extent || data->extstart > pos ||
		    data->extino != ino || data->exttree != tree ||
		    data->extend <= pos) {
			struct btrfs_key key_in, key_out;
			phys_addr_t elemaddr;
			size_t elemsize;

			free(data->extent);
			key_in.object_id = ino;
			key_in.type = BTRFS_ITEM_TYPE_EXTENT_ITEM;
			key_in.offset = cpu_to_le64(pos);
			err = lower_bound(data, &key_in, &key_out, tree,
					  &elemaddr, &elemsize, &desc, 0);
			if (err) {
				free(desc.data);
				return -1;
			}
			if (key_out.object_id != ino ||
			    key_out.type != BTRFS_ITEM_TYPE_EXTENT_ITEM) {
				pr_err("extent not found");
				return -EBADR;
			}
			if ((ssize_t)elemsize < ((char *)&data->extent->inl -
						 (char *)data->extent)) {
				pr_err("extent descriptor is too short");
				return -EBADR;
			}
			data->extstart = le_to_cpu64(key_out.offset);
			data->extsize = elemsize;
			data->extent = malloc(elemsize);
			data->extino = ino;
			data->exttree = tree;
			if (!data->extent)
				return errno;

			err = btrfs_read_logical(data, elemaddr, data->extent,
						 elemsize, 0);
			if (err)
				return err;

			data->extend = data->extstart +
				       le_to_cpu64(data->extent->size);
			if (data->extent->type == BTRFS_EXTENT_REGULAR &&
			    (char *)data->extent + elemsize >=
				    (char *)&data->extent->filled +
					    sizeof(data->extent->filled))
				data->extend =
					data->extstart +
					le_to_cpu64(data->extent->filled);

			pr_info(
				"regular extent 0x%" PRIxUINT64_T
				"+0x%" PRIxUINT64_T "\n",
				le_to_cpu64(key_out.offset),
				le_to_cpu64(data->extent->size));
			/*
	   * The way of extent item iteration is pretty bad, it completely
	   * requires all extents are contiguous, which is not ensured.
	   *
	   * Features like NO_HOLE and mixed inline/regular extents can cause
	   * gaps between file extent items.
	   *
	   * The correct way is to follow Linux kernel/U-boot to iterate item
	   * by item, without any assumption on the file offset continuity.
	   *
	   * Here we just manually skip to next item and re-do the verification.
	   *
	   * TODO: Rework the whole extent item iteration code, if not the
	   * whole btrfs implementation.
	   */
			if (data->extend <= pos) {
				err = next(data, &desc, &elemaddr, &elemsize,
					   &key_out);
				if (err < 0)
					return -1;
				/* No next item for the inode, we hit the end. */
				if (err == 0 || key_out.object_id != ino ||
				    key_out.type !=
					    BTRFS_ITEM_TYPE_EXTENT_ITEM)
					return pos - pos0;

				csize = le_to_cpu64(key_out.offset) - pos;
				if (csize > len)
					csize = len;

				memset(buf, 0, csize);
				buf += csize;
				pos += csize;
				len -= csize;
				continue;
			}
		}
		csize = data->extend - pos;
		extoff = pos - data->extstart;
		if (csize > len)
			csize = len;

		if (data->extent->encryption) {
			pr_err("encryption not supported");
			return -EOPNOTSUPP;
		}

		if (data->extent->compression != BTRFS_COMPRESSION_NONE &&
		    data->extent->compression != BTRFS_COMPRESSION_ZLIB &&
		    data->extent->compression != BTRFS_COMPRESSION_LZO &&
		    data->extent->compression != BTRFS_COMPRESSION_ZSTD) {
			pr_err("compression type 0x%x not supported",
			      data->extent->compression);
			return -EOPNOTSUPP;
		}

		if (data->extent->encoding) {
			pr_err("encoding not supported");
			return -EOPNOTSUPP;
		}

		switch (data->extent->type) {
		case BTRFS_EXTENT_INLINE:
			if (data->extent->compression ==
			    BTRFS_COMPRESSION_ZLIB) {
				if (zlib_decompress(
					    data->extent->inl,
					    data->extsize -
						    ((uint8_t *)
							     data->extent->inl -
						     (uint8_t *)data->extent),
					    extoff, buf,
					    csize) != (ssize_t)csize) {
					if (!errno) {
						pr_err("premature end of compressed");
						return -EINVAL;
					}
				}
			} else if (data->extent->compression ==
				   BTRFS_COMPRESSION_LZO) {
				if (btrfs_lzo_decompress(
					    data->extent->inl,
					    data->extsize -
						    ((uint8_t *)
							     data->extent->inl -
						     (uint8_t *)data->extent),
					    extoff, buf,
					    csize) != (ssize_t)csize)
					return -1;
			} else if (data->extent->compression ==
				   BTRFS_COMPRESSION_ZSTD) {
				if (btrfs_zstd_decompress(
					    data->extent->inl,
					    data->extsize -
						    ((uint8_t *)
							     data->extent->inl -
						     (uint8_t *)data->extent),
					    extoff, buf,
					    csize) != (ssize_t)csize)
					return -1;
			} else
				memcpy(buf, data->extent->inl + extoff, csize);
			break;
		case BTRFS_EXTENT_REGULAR:
			if (!data->extent->laddr) {
				memset(buf, 0, csize);
				break;
			}

			if (data->extent->compression !=
			    BTRFS_COMPRESSION_NONE) {
				char *tmp;
				uint64_t zsize;
				ssize_t ret;

				zsize = le_to_cpu64(
					data->extent->compressed_size);
				tmp = malloc(zsize);
				if (!tmp)
					return -1;
				err = btrfs_read_logical(
					data, le_to_cpu64(data->extent->laddr),
					tmp, zsize, 0);
				if (err) {
					free(tmp);
					return -1;
				}

				if (data->extent->compression ==
				    BTRFS_COMPRESSION_ZLIB)
					ret = zlib_decompress(
						tmp, zsize,
						extoff +
							le_to_cpu64(
								data->extent
									->offset),
						buf, csize);
				else if (data->extent->compression ==
					 BTRFS_COMPRESSION_LZO)
					ret = btrfs_lzo_decompress(
						tmp, zsize,
						extoff +
							le_to_cpu64(
								data->extent
									->offset),
						buf, csize);
				else if (data->extent->compression ==
					 BTRFS_COMPRESSION_ZSTD)
					ret = btrfs_zstd_decompress(
						tmp, zsize,
						extoff +
							le_to_cpu64(
								data->extent
									->offset),
						buf, csize);
				else
					ret = -1;

				free(tmp);

				if (ret != (ssize_t)csize) {
					if (!errno) {
						pr_err("premature end of compressed");
						return -EINVAL;
					}
				}

				break;
			}
			err = btrfs_read_logical(
				data,
				le_to_cpu64(data->extent->laddr) +
					le_to_cpu64(data->extent->offset) +
					extoff,
				buf, csize, 0);
			if (err)
				return -1;
			break;
		default:
			pr_err("unsupported extent type 0x%x",
			      data->extent->type);
			return -EOPNOTSUPP;
		}
		buf += csize;
		pos += csize;
		len -= csize;
	}
	return pos - pos0;
}

static int get_root(struct btrfs_data *data, struct btrfs_key *key,
		      uint64_t *tree, uint8_t *type)
{
	int err;
	phys_addr_t elemaddr;
	size_t elemsize;
	struct btrfs_key key_out, key_in;
	struct btrfs_root_item ri;

	key_in.object_id =
		cpu_to_le64_compile_time(BTRFS_ROOT_VOL_OBJECTID);
	key_in.offset = 0;
	key_in.type = BTRFS_ITEM_TYPE_ROOT_ITEM;
	err = lower_bound(data, &key_in, &key_out, data->sblock.root_tree,
			  &elemaddr, &elemsize, NULL, 0);
	if (err)
		return err;
	if (key_in.object_id != key_out.object_id ||
	    key_in.type != key_out.type || key_in.offset != key_out.offset) {
			pr_err("no root");
			return -EBADR;
		}
	err = btrfs_read_logical(data, elemaddr, &ri, sizeof(ri), 0);
	if (err)
		return err;
	key->type = BTRFS_ITEM_TYPE_DIR_ITEM;
	key->offset = 0;
	key->object_id = cpu_to_le64_compile_time(BTRFS_OBJECT_ID_CHUNK);
	*tree = ri.tree;
	*type = BTRFS_DIR_ITEM_TYPE_DIRECTORY;
	return 0;
}

static int find_path(struct btrfs_data *data, const char *path,
		       struct btrfs_key *key, uint64_t *tree, uint8_t *type)
{
	const char *slash = path;
	int err;
	phys_addr_t elemaddr;
	size_t elemsize;
	size_t allocated = 0;
	struct btrfs_dir_item *direl = NULL;
	struct btrfs_key key_out;
	const char *ctoken;
	size_t ctokenlen;
	char *path_alloc = NULL;
	char *origpath = NULL;
	unsigned symlinks_max = 32;
	size_t sz;

	err = get_root(data, key, tree, type);
	if (err)
		return err;

	origpath = strdup(path);
	if (!origpath)
		return errno;

	while (1) {
		while (path[0] == '/')
			path++;
		if (!path[0])
			break;
		slash = strchr(path, '/');
		if (!slash)
			slash = path + strlen(path);
		ctoken = path;
		ctokenlen = slash - path;

		if (*type != BTRFS_DIR_ITEM_TYPE_DIRECTORY) {
			free(path_alloc);
			free(origpath);
			pr_err("not a directory");
			return -EBADR;
		}

		if (ctokenlen == 1 && ctoken[0] == '.') {
			path = slash;
			continue;
		}
		if (ctokenlen == 2 && ctoken[0] == '.' && ctoken[1] == '.') {
			key->type = BTRFS_ITEM_TYPE_INODE_REF;
			key->offset = -1;

			err = lower_bound(data, key, &key_out, *tree, &elemaddr,
					  &elemsize, NULL, 0);
			if (err) {
				free(direl);
				free(path_alloc);
				free(origpath);
				return err;
			}

			if (key_out.type != key->type ||
			    key->object_id != key_out.object_id) {
				free(direl);
				free(path_alloc);
				pr_err("file '%s' not found\n", origpath);
				err = -ENOENT;
				free(origpath);
				return err;
			}

			*type = BTRFS_DIR_ITEM_TYPE_DIRECTORY;
			key->object_id = key_out.offset;

			path = slash;

			continue;
		}

		key->type = BTRFS_ITEM_TYPE_DIR_ITEM;
		key->offset = cpu_to_le64(~getcrc32c(1, ctoken, ctokenlen));

		err = lower_bound(data, key, &key_out, *tree, &elemaddr,
				  &elemsize, NULL, 0);
		if (err) {
			free(direl);
			free(path_alloc);
			free(origpath);
			return err;
		}
		if (key_cmp(key, &key_out) != 0) {
			free(direl);
			free(path_alloc);
			err = -ENOENT;
			pr_err("file `%s' not found\n", origpath);
			free(origpath);
			return err;
		}

		struct btrfs_dir_item *cdirel;
		if (elemsize > allocated) {
			if (mul(2, elemsize, &allocated) ||
			    add(allocated, 1, &sz)) {
				free(path_alloc);
				free(origpath);
				pr_err("directory item size overflow\n");
				return -ERANGE;
			}
			free(direl);
			direl = malloc(sz);
			if (!direl) {
				free(path_alloc);
				free(origpath);
				return errno;
			}
		}

		err = btrfs_read_logical(data, elemaddr, direl, elemsize, 0);
		if (err) {
			free(direl);
			free(path_alloc);
			free(origpath);
			return err;
		}

		for (cdirel = direl;
		     (uint8_t *)cdirel - (uint8_t *)direl < (ssize_t)elemsize;
		     cdirel = (void *)((uint8_t *)(direl + 1) +
				       le_to_cpu16(cdirel->n) +
				       le_to_cpu16(cdirel->m))) {
			if (ctokenlen == le_to_cpu16(cdirel->n) &&
			    memcmp(cdirel->name, ctoken, ctokenlen) == 0)
				break;
		}
		if ((uint8_t *)cdirel - (uint8_t *)direl >= (ssize_t)elemsize) {
			free(direl);
			free(path_alloc);
			pr_err("file `%s' not found\n", origpath);

			err = -ENOENT;
			free(origpath);
			return err;
		}

		path = slash;
		if (cdirel->type == BTRFS_DIR_ITEM_TYPE_SYMLINK) {
			struct btrfs_inode inode;
			char *tmp;
			if (--symlinks_max == 0) {
				free(direl);
				free(path_alloc);
				free(origpath);
				pr_err("too deep nesting of symlinks\n");
				return -ELOOP;
			}

			err = btrfs_read_inode(data, &inode,
					       cdirel->key.object_id, *tree);
			if (err) {
				free(direl);
				free(path_alloc);
				free(origpath);
				return err;
			}

			if (add(le_to_cpu64(inode.size), strlen(path), &sz) ||
			    add(sz, 1, &sz)) {
				free(direl);
				free(path_alloc);
				free(origpath);
				pr_err("buffer size overflow\n");
				return -ERANGE;
			}
			tmp = malloc(sz);
			if (!tmp) {
				free(direl);
				free(path_alloc);
				free(origpath);
				return errno;
			}

			if (btrfs_extent_read(data, cdirel->key.object_id,
					      *tree, 0, tmp,
					      le_to_cpu64(inode.size)) !=
			    (ssize_t)le_to_cpu64(inode.size)) {
				free(direl);
				free(path_alloc);
				free(origpath);
				free(tmp);
				return errno;
			}
			memcpy(tmp + le_to_cpu64(inode.size), path,
			       strlen(path) + 1);
			free(path_alloc);
			path = path_alloc = tmp;
			if (path[0] == '/') {
				err = get_root(data, key, tree, type);
				if (err) {
					free(direl);
					free(path_alloc);
					free(origpath);
					return err;
				}
			}
			continue;
		}
		*type = cdirel->type;

		switch (cdirel->key.type) {
		case BTRFS_ITEM_TYPE_ROOT_ITEM: {
			struct btrfs_root_item ri;
			err = lower_bound(data, &cdirel->key, &key_out,
					  data->sblock.root_tree, &elemaddr,
					  &elemsize, NULL, 0);
			if (err) {
				free(direl);
				free(path_alloc);
				free(origpath);
				return err;
			}
			if (cdirel->key.object_id != key_out.object_id ||
			    cdirel->key.type != key_out.type) {
				free(direl);
				free(path_alloc);
				pr_err("file `%s' not found\n", origpath);
				err = -ENOENT;
				free(origpath);
				return err;
			}
			err = btrfs_read_logical(data, elemaddr, &ri,
						 sizeof(ri), 0);
			if (err) {
				free(direl);
				free(path_alloc);
				free(origpath);
				return err;
			}
			key->type = BTRFS_ITEM_TYPE_DIR_ITEM;
			key->offset = 0;
			key->object_id = cpu_to_le64_compile_time(
				BTRFS_OBJECT_ID_CHUNK);
			*tree = ri.tree;
			break;
		}
		case BTRFS_ITEM_TYPE_INODE_ITEM:
			if (*slash &&
			    *type == BTRFS_DIR_ITEM_TYPE_REGULAR) {
				free(direl);
				free(path_alloc);
				pr_err("file `%s' not found\n", origpath);
				err = -ENOENT;
				free(origpath);
				return err;
			}
			*key = cdirel->key;
			if (*type == BTRFS_DIR_ITEM_TYPE_DIRECTORY)
				key->type = BTRFS_ITEM_TYPE_DIR_ITEM;
			break;
		default:
			free(path_alloc);
			free(origpath);
			free(direl);
			return -EBADR;
		}
	}

	free(direl);
	free(origpath);
	free(path_alloc);
	return 0;
}

static int btrfs_dir(struct device device, const char *path, fs_dir_hook_t hook,
		       void *hook_data)
{
	struct btrfs_data *data = btrfs_mount(device);
	struct btrfs_key key_in, key_out;
	int err;
	phys_addr_t elemaddr;
	size_t elemsize;
	size_t allocated = 0;
	struct btrfs_dir_item *direl = NULL;
	struct btrfs_leaf_descriptor desc;
	int r = 0;
	uint64_t tree;
	uint8_t type;
	size_t est_size = 0;
	size_t sz;

	if (!data)
		return errno;

	err = find_path(data, path, &key_in, &tree, &type);
	if (err) {
		btrfs_unmount(data);
		return err;
	}
	if (type != BTRFS_DIR_ITEM_TYPE_DIRECTORY) {
		btrfs_unmount(data);
		pr_err("Not a directory\n");
		return -EBADR;
	}

	err = lower_bound(data, &key_in, &key_out, tree, &elemaddr, &elemsize,
			  &desc, 0);
	if (err) {
		btrfs_unmount(data);
		free(desc.data);
		return err;
	}
	if (key_out.type != BTRFS_ITEM_TYPE_DIR_ITEM ||
	    key_out.object_id != key_in.object_id) {
		r = next(data, &desc, &elemaddr, &elemsize, &key_out);
		if (r <= 0)
			goto out;
	}
	do {
		struct btrfs_dir_item *cdirel;
		if (key_out.type != BTRFS_ITEM_TYPE_DIR_ITEM ||
		    key_out.object_id != key_in.object_id) {
			r = 0;
			break;
		}
		if (elemsize > allocated) {
			if (mul(2, elemsize, &allocated) ||
			    add(allocated, 1, &sz)) {
				pr_err("directory element size overflow\n");
				r = -ERANGE;
				break;
			}
			free(direl);
			direl = malloc(sz);
			if (!direl) {
				r = -errno;
				break;
			}
		}

		err = btrfs_read_logical(data, elemaddr, direl, elemsize, 0);
		if (err) {
			r = -err;
			break;
		}

		if (direl == NULL ||
		    add(le_to_cpu16(direl->n), le_to_cpu16(direl->m),
			&est_size) ||
		    add(est_size, sizeof(*direl), &est_size) ||
		    sub(est_size, sizeof(direl->name), &est_size) ||
		    est_size > allocated) {
			errno = -ERANGE;
			r = -errno;
			goto out;
		}

		for (cdirel = direl;
		     (uint8_t *)cdirel - (uint8_t *)direl < (ssize_t)elemsize;
		     cdirel = (void *)((uint8_t *)(direl + 1) +
				       le_to_cpu16(cdirel->n) +
				       le_to_cpu16(cdirel->m))) {
			char c;
			struct btrfs_inode inode;
			struct dirhook_info info;

			if (cdirel == NULL ||
			    add(le_to_cpu16(cdirel->n), le_to_cpu16(cdirel->m),
				&est_size) ||
			    add(est_size, sizeof(*cdirel), &est_size) ||
			    sub(est_size, sizeof(cdirel->name), &est_size) ||
			    est_size > allocated) {
				errno = -ERANGE;
				r = -errno;
				goto out;
			}

			err = btrfs_read_inode(data, &inode,
					       cdirel->key.object_id, tree);
			memset(&info, 0, sizeof(info));
			if (err)
				errno = 0;
			else {
				info.mtime = le_to_cpu64(inode.mtime.sec);
				info.mtimeset = 1;
			}
			c = cdirel->name[le_to_cpu16(cdirel->n)];
			cdirel->name[le_to_cpu16(cdirel->n)] = 0;
			info.dir = (cdirel->type ==
				    BTRFS_DIR_ITEM_TYPE_DIRECTORY);
			if (hook(cdirel->name, &info, hook_data))
				goto out;
			cdirel->name[le_to_cpu16(cdirel->n)] = c;
		}
		r = next(data, &desc, &elemaddr, &elemsize, &key_out);
	} while (r > 0);

out:
	free(direl);

	free_iterator(&desc);
	btrfs_unmount(data);

	return -r;
}

static int btrfs_open(struct file *file, const char *name)
{
	struct btrfs_data *data = btrfs_mount(file->device);
	int err;
	struct btrfs_inode inode;
	uint8_t type;
	struct btrfs_key key_in;

	if (!data)
		return errno;

	err = find_path(data, name, &key_in, &data->tree, &type);
	if (err) {
		btrfs_unmount(data);
		return err;
	}
	if (type != BTRFS_DIR_ITEM_TYPE_REGULAR) {
		btrfs_unmount(data);
		return -ENOENT;
	}

	data->inode = key_in.object_id;
	err = btrfs_read_inode(data, &inode, data->inode, data->tree);
	if (err) {
		btrfs_unmount(data);
		return err;
	}

	file->data = data;
	file->size = le_to_cpu64(inode.size);

	return err;
}

static int btrfs_close(struct file file)
{
	btrfs_unmount(file->data);

	return 0;
}

static ssize_t btrfs_read(struct file *file, char *buf, size_t len)
{
	struct btrfs_data *data = file->data;

	return btrfs_extent_read(data, data->inode, data->tree, file->offset,
				 buf, len);
}

static int btrfs_uuid(struct device device, char **uuid)
{
	struct btrfs_data *data;

	*uuid = NULL;

	data = btrfs_mount(device);
	if (!data)
		return errno;

	*uuid = xasprintf("%04x%04x-%04x-%04x-%04x-%04x%04x%04x",
			  be_to_cpu16(data->sblock.uuid[0]),
			  be_to_cpu16(data->sblock.uuid[1]),
			  be_to_cpu16(data->sblock.uuid[2]),
			  be_to_cpu16(data->sblock.uuid[3]),
			  be_to_cpu16(data->sblock.uuid[4]),
			  be_to_cpu16(data->sblock.uuid[5]),
			  be_to_cpu16(data->sblock.uuid[6]),
			  be_to_cpu16(data->sblock.uuid[7]));

	btrfs_unmount(data);

	return errno;
}

static int btrfs_label(struct device device, char **label)
{
	struct btrfs_data *data;

	*label = NULL;

	data = btrfs_mount(device);
	if (!data)
		return errno;

	*label = strndup(data->sblock.label, sizeof(data->sblock.label));

	btrfs_unmount(data);

	return errno;
}

#ifdef UTIL

struct embed_region {
	unsigned int start;
	unsigned int secs;
};

/*
 * https://btrfs.wiki.kernel.org/index.php/Manpage/btrfs(5)#BOOTLOADER_SUPPORT
 * The first 1 MiB on each device is unused with the exception of primary
 * superblock that is on the offset 64 KiB and spans 4 KiB.
 */

static const struct {
	struct embed_region available;
	struct embed_region used[6];
} btrfs_head = { .available = { 0, DISK_KiB_TO_SECTORS(
					   1024) }, /* The first 1 MiB. */
		 .used = {
			 { 0, 1 }, /* boot.S. */
			 { DISK_KiB_TO_SECTORS(64) - 1,
			   1 }, /* Overflow guard. */
			 { DISK_KiB_TO_SECTORS(64),
			   DISK_KiB_TO_SECTORS(4) }, /* 4 KiB superblock. */
			 { DISK_KiB_TO_SECTORS(68),
			   1 }, /* Overflow guard. */
			 { DISK_KiB_TO_SECTORS(1024) - 1,
			   1 }, /* Overflow guard. */
			 { 0, 0 } /* Array terminator. */
		 } };

static int btrfs_embed(struct device device __attribute__((unused)),
			 unsigned int *nsectors, unsigned int max_nsectors,
			 embed_type_t embed_type, phys_addr_t **sectors)
{
	unsigned int i, j, n = 0;
	const struct embed_region *u;
	phys_addr_t *map;

	if (embed_type != EMBED_PCBIOS) {
		pr_err("BtrFS currently supports only PC-BIOS embedding\n");
		return -EINVAL;
	}

	map = calloc(btrfs_head.available.secs, sizeof(*map));
	if (map == NULL)
		return errno;

	/*
   * Populating the map array so that it can be used to index if a disk
   * address is available to embed:
   *   - 0: available,
   *   - 1: unavailable.
   */
	for (u = btrfs_head.used; u->secs; ++u) {
		unsigned int end = u->start + u->secs;

		if (end > btrfs_head.available.secs)
			end = btrfs_head.available.secs;
		for (i = u->start; i < end; ++i)
			map[i] = 1;
	}

	/* Adding up n until it matches total size of available embedding area. */
	for (i = 0; i < btrfs_head.available.secs; ++i)
		if (map[i] == 0)
			n++;

	if (n < *nsectors) {
		free(map);
		pr_err("your core.img is unusually large. It won't fit in the embedding area\n");
		return -ERANGE;
	}

	if (n > max_nsectors)
		n = max_nsectors;

	/*
   * Populating the array so that it can used to index disk block address for
   * an image file's offset to be embedded on disk (the unit is in sectors):
   *   - i: The disk block address relative to btrfs_head.available.start,
   *   - j: The offset in image file.
   */
	for (i = 0, j = 0; i < btrfs_head.available.secs && j < n; ++i)
		if (map[i] == 0)
			map[j++] = btrfs_head.available.start + i;

	*nsectors = n;
	*sectors = map;

	return 0;
}
#endif
