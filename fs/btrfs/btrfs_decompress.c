
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

	total_size = le32_to_cpu(get_unaligned32(ibuf));
	ibuf += sizeof(total_size);

	if (isize < total_size)
		return -1;

	/* Jump forward to first block with requested data.  */
	while (off >= BTRFS_LZO_BLOCK_SIZE) {
		/* Don't let following uint32_t cross the page boundary.  */
		if (((ibuf - ibuf0) & 0xffc) == 0xffc)
			ibuf = ((ibuf - ibuf0 + 3) & ~3) + ibuf0;

		cblock_size = le32_to_cpu(get_unaligned32(ibuf));
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

		cblock_size = le32_to_cpu(get_unaligned32(ibuf));
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
		if (lzo1x_decompress_safe((lzo_bytep)ibuf, cblock_s,                                                                                                                                                                                  ize,
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
