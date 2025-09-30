#ifndef __BTRFS_H__
#define __BTRFS_H__
#include <linux/types.h>
#include <linux/sizes.h>

#define BTRFS_SIGNATURE "_BHRfS_M"

/* From http://www.oberhumer.com/opensource/lzo/lzofaq.php
 * LZO will expand incompressible data by a little amount. I still haven't
 * computed the exact values, but I suggest using these formulas for
 * a worst-case expansion calculation:
 *
 * output_block_size = input_block_size + (input_block_size / 16) + 64 + 3
 *  */
#define BTRFS_LZO_BLOCK_SIZE 4096
#define BTRFS_LZO_BLOCK_MAX_CSIZE \
	(BTRFS_LZO_BLOCK_SIZE + (BTRFS_LZO_BLOCK_SIZE / 16) + 64 + 3)

#define ZSTD_BTRFS_MAX_WINDOWLOG 17
#define ZSTD_BTRFS_MAX_INPUT (1 << ZSTD_BTRFS_MAX_WINDOWLOG)

typedef uint8_t btrfs_checksum_t[0x20];
typedef uint16_t btrfs_uuid_t[8];

struct btrfs_device {
	uint64_t device_id;
	uint64_t size;
	uint8_t dummy[0x62 - 0x10];
} PACKED;

struct btrfs_superblock {
	btrfs_checksum_t checksum;
	btrfs_uuid_t uuid;
	uint8_t dummy[0x10];
	uint8_t signature[sizeof(BTRFS_SIGNATURE) - 1];
	uint64_t generation;
	uint64_t root_tree;
	uint64_t chunk_tree;
	uint8_t dummy2[0x20];
	uint64_t root_dir_objectid;
	uint8_t dummy3[0x41];
	struct btrfs_device this_device;
	char label[0x100];
	uint8_t dummy4[0x100];
	uint8_t bootstrap_mapping[0x800];
} PACKED;

struct btrfs_header {
	btrfs_checksum_t checksum;
	btrfs_uuid_t uuid;
	uint64_t bytenr;
	uint8_t dummy[0x28];
	uint32_t nitems;
	uint8_t level;
} PACKED;

struct btrfs_device_desc {
	device_t dev;
	uint64_t id;
};

struct btrfs_data {
	struct btrfs_superblock sblock;
	uint64_t tree;
	uint64_t inode;

	struct btrfs_device_desc *devices_attached;
	unsigned n_devices_attached;
	unsigned n_devices_allocated;

	/* Cached extent data.  */
	uint64_t extstart;
	uint64_t extend;
	uint64_t extino;
	uint64_t exttree;
	size_t extsize;
	struct btrfs_extent_data *extent;
};

struct btrfs_chunk_item {
	uint64_t size;
	uint64_t dummy;
	uint64_t stripe_length;
	uint64_t type;
#define BTRFS_CHUNK_TYPE_BITS_DONTCARE 0x07
#define BTRFS_CHUNK_TYPE_SINGLE 0x00
#define BTRFS_CHUNK_TYPE_RAID0 0x08
#define BTRFS_CHUNK_TYPE_RAID1 0x10
#define BTRFS_CHUNK_TYPE_DUPLICATED 0x20
#define BTRFS_CHUNK_TYPE_RAID10 0x40
#define BTRFS_CHUNK_TYPE_RAID5 0x80
#define BTRFS_CHUNK_TYPE_RAID6 0x100
#define BTRFS_CHUNK_TYPE_RAID1C3 0x200
#define BTRFS_CHUNK_TYPE_RAID1C4 0x400
	uint8_t dummy2[0xc];
	uint16_t nstripes;
	uint16_t nsubstripes;
} PACKED;

struct btrfs_chunk_stripe {
	uint64_t device_id;
	uint64_t offset;
	btrfs_uuid_t device_uuid;
} PACKED;

struct btrfs_leaf_node {
	struct btrfs_key key;
	uint32_t offset;
	uint32_t size;
} PACKED;

struct btrfs_internal_node {
	struct btrfs_key key;
	uint64_t addr;
	uint64_t dummy;
} PACKED;

struct btrfs_dir_item {
	struct btrfs_key key;
	uint8_t dummy[8];
	uint16_t m;
	uint16_t n;
#define BTRFS_DIR_ITEM_TYPE_REGULAR 1
#define BTRFS_DIR_ITEM_TYPE_DIRECTORY 2
#define BTRFS_DIR_ITEM_TYPE_SYMLINK 7
	uint8_t type;
	char name[0];
} PACKED;

struct btrfs_leaf_descriptor {
	unsigned depth;
	unsigned allocated;
	struct {
		phys_addr_t addr;
		unsigned iter;
		unsigned maxiter;
		int leaf;
	} * data;
};

struct btrfs_time {
	int64_t sec;
	uint32_t nanosec;
} PACKED;

struct btrfs_inode {
	uint8_t dummy1[0x10];
	uint64_t size;
	uint8_t dummy2[0x70];
	struct btrfs_time mtime;
} PACKED;

struct btrfs_extent_data {
	uint64_t dummy;
	uint64_t size;
	uint8_t compression;
	uint8_t encryption;
	uint16_t encoding;
	uint8_t type;
	union {
		char inl[0];
		struct {
			uint64_t laddr;
			uint64_t compressed_size;
			uint64_t offset;
			uint64_t filled;
		};
	};
} PACKED;

#define BTRFS_EXTENT_INLINE 0
#define BTRFS_EXTENT_REGULAR 1

#define BTRFS_COMPRESSION_NONE 0
#define BTRFS_COMPRESSION_ZLIB 1
#define BTRFS_COMPRESSION_LZO 2
#define BTRFS_COMPRESSION_ZSTD 3

#define BTRFS_OBJECT_ID_CHUNK 0x100


enum
  {
    BTRFS_ITEM_TYPE_INODE_ITEM = 0x01,
    BTRFS_ITEM_TYPE_INODE_REF = 0x0c,
    BTRFS_ITEM_TYPE_DIR_ITEM = 0x54,
    BTRFS_ITEM_TYPE_EXTENT_ITEM = 0x6c,
    BTRFS_ITEM_TYPE_ROOT_ITEM = 0x84,
    BTRFS_ITEM_TYPE_ROOT_BACKREF = 0x90,
    BTRFS_ITEM_TYPE_DEVICE = 0xd8,
    BTRFS_ITEM_TYPE_CHUNK = 0xe4
  };

enum
  {
    BTRFS_ROOT_VOL_OBJECTID = 5,
    BTRFS_TREE_ROOT_OBJECTID = 0x100,
  };

struct btrfs_root_item
{
  uint8_t dummy[0xb0];
  uint64_t tree;
  uint64_t inode;
};

struct btrfs_key
{
  uint64_t object_id;
  uint8_t type;
  uint64_t offset;
} PACKED;


struct btrfs_root_backref
{
  uint64_t inode_id;
  uint64_t seqnr;
  uint16_t n;
  char name[0];
};

struct btrfs_inode_ref
{
  uint64_t idxid;
  uint16_t n;
  char name[0];
};


#endif