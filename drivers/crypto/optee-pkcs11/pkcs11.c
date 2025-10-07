// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018-2019 Linaro Ltd.
 */

#include <of.h>
#include <linux/hw_random.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/tee_drv.h>
#include <linux/uuid.h>
#include <crypto/pkcs11.h>
#include "pkcs11_ta.h"

#define DRIVER_NAME "optee-pkcs11"

#define TEE_ERROR_HEALTH_TEST_FAIL	0x00000001

/*
 * TA_CMD_GET_RNG_INFO - Get RNG information
 *
 * param[0] (out value) - value.a: RNG data-rate in bytes per second
 *                        value.b: Quality/Entropy per 1024 bit of data
 * param[1] unused
 * param[2] unused
 * param[3] unused
 *
 * Result:
 * TEE_SUCCESS - Invoke command success
 * TEE_ERROR_BAD_PARAMETERS - Incorrect input param
 */
#define TA_CMD_GET_RNG_INFO		0x1

#define MAX_ENTROPY_REQ_SZ		(4 * 1024)

/**
 * struct optee_pkcs11_private - OP-TEE Random Number Generator private data
 * @dev:		OP-TEE based RNG device.
 * @ctx:		OP-TEE context handler.
 * @session_id:		RNG TA session identifier.
 * @data_rate:		RNG data rate.
 * @entropy_shm_pool:	Memory pool shared with RNG device.
 * @optee_rng:		OP-TEE RNG driver structure.
 */
struct optee_pkcs11_private {
	u32 session_id;
    
    struct device *dev;
	struct tee_context *ctx;
    struct tee_shm *entropy_shm_pool;
	struct pkcs11 optee_cki;
};

#define to_optee_pkcs11_private(r) \
		container_of(r, struct optee_pkcs11_private, optee_cki)
/*
static size_t get_optee_pkcs11_data(struct optee_pkcs11_private *pvt_data,
				 void *buf, size_t req_size)
{
	int ret = 0;
	u8 *pkcs11_data = NULL;
	size_t pkcs11_size = 0;
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	inv_arg.func = PKCS11_CMD_PING;
	inv_arg.session = pvt_data->session_id;
	inv_arg.num_params = 4;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	param[0].u.memref.shm = pvt_data->entropy_shm_pool;
	param[0].u.memref.size = req_size;
	param[0].u.memref.shm_offs = 0;

	ret = tee_client_invoke_func(pvt_data->ctx, &inv_arg, param);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		dev_err(pvt_data->dev, "PKCS11_CMD_PING invoke err: %x\n",
			inv_arg.ret);
		return 0;
	}

	pkcs11_data = tee_shm_get_va(pvt_data->entropy_shm_pool, 0);
	if (IS_ERR(pkcs11_data)) {
		dev_err(pvt_data->dev, "tee_shm_get_va failed\n");
		return 0;
	}

	pkcs11_size = param[0].u.memref.size;
	memcpy(buf, pkcs11_data, pkcs11_size);

	return pkcs11_size;
}

static int optee_pkcs11_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct optee_pkcs11_private *pvt_data = to_optee_pkcs11_private(rng);
	size_t read = 0, pkcs11_size;
	int timeout = 1;
	u8 *data = buf;
	
	if (max > MAX_ENTROPY_REQ_SZ)
	max = MAX_ENTROPY_REQ_SZ;

	while (read < max) {
		pkcs11_size = get_optee_pkcs11_data(pvt_data, data, (max - read));

		data += pkcs11_size;
		read += pkcs11_size;

		if (wait) {
			if ((timeout-- == 0) || (read == max))
			return read;
		} else {
			return read;
		}
	}
	
	return read;
}
*/

static int optee_pkcs11_init(struct pkcs11 *tee_cki)
{
	struct optee_pkcs11_private *pvt_data = to_optee_pkcs11_private(tee_cki);
	struct tee_shm *entropy_shm_pool = NULL;

	entropy_shm_pool = tee_shm_alloc_kernel_buf(pvt_data->ctx,
						    MAX_ENTROPY_REQ_SZ);
	if (IS_ERR(entropy_shm_pool)) {
		dev_err(pvt_data->dev, "tee_shm_alloc_kernel_buf failed\n");
		return PTR_ERR(entropy_shm_pool);
	}

	pvt_data->entropy_shm_pool = entropy_shm_pool;
    
    pr_info("This is a pkcs11 optee %s\n", tee_cki->name);

	return 0;
}

static struct pkcs11_cryptoki_ops optee_pkcs11_ops = {
    .init       = optee_pkcs11_init,
};

static struct optee_pkcs11_private pvt_data = {
	.optee_cki = {
		.name		= DRIVER_NAME,
        .ops        = &optee_pkcs11_ops,
	}
};

static int get_optee_pkcs11_info(struct device *dev)
{
//	int ret = 0;
//	struct tee_ioctl_invoke_arg inv_arg;
//	struct tee_param param[4];
//
//	memset(&inv_arg, 0, sizeof(inv_arg));
//	memset(&param, 0, sizeof(param));
//
//	inv_arg.func = TA_CMD_GET_RNG_INFO;
//	inv_arg.session = pvt_data.session_id;
//	inv_arg.num_params = 4;
//
//	/* Fill invoke cmd params */
//	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT;
//
//	ret = tee_client_invoke_func(pvt_data.ctx, &inv_arg, param);
//	if ((ret < 0) || (inv_arg.ret != 0)) {
//		dev_err(dev, "TA_CMD_GET_RNG_INFO invoke err: %x\n",
//			inv_arg.ret);
//		return -EINVAL;
//	}
//
//	pvt_data.data_rate = param[0].u.value.a;
//	pvt_data.quality = param[0].u.value.b;

	return 0;
}

static void optee_pkcs11_devinfo(struct device *dev)
{
	printf("OPTEE PKCS11\n");
}

static int optee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	if (ver->impl_id == TEE_IMPL_ID_OPTEE)
		return 1;
	else
		return 0;
}

static int optee_pkcs11_probe(struct device *dev)
{
	struct tee_client_device *pkcs11_device = to_tee_client_device(dev);
	int ret = 0, err = -ENODEV;
	struct tee_ioctl_open_session_arg sess_arg;

	memset(&sess_arg, 0, sizeof(sess_arg));

	/* Open context with TEE driver */
	pvt_data.ctx = tee_client_open_context(NULL, optee_ctx_match, NULL,
					       NULL);
	if (IS_ERR(pvt_data.ctx))
		return -ENODEV;

	/* Open session with hwrng Trusted App */
	export_uuid(sess_arg.uuid, &pkcs11_device->id.uuid);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	sess_arg.num_params = 0;

	ret = tee_client_open_session(pvt_data.ctx, &sess_arg, NULL);
	if ((ret < 0) || (sess_arg.ret != 0)) {
		dev_err(dev, "tee_client_open_session failed, err: %x\n",
			sess_arg.ret);
		err = -EINVAL;
		goto out_ctx;
	}
	pvt_data.session_id = sess_arg.session;

	err = get_optee_pkcs11_info(dev);
	if (err)
		goto out_sess;

	err = pkcs11_register(dev, &pvt_data.optee_cki);
	if (err) {
		dev_err(dev, "hwrng registration failed (%d)\n", err);
		goto out_sess;
	}

	pvt_data.dev = dev;
	devinfo_add(dev, optee_pkcs11_devinfo);

	return 0;

out_sess:
	tee_client_close_session(pvt_data.ctx, pvt_data.session_id);
out_ctx:
	tee_client_close_context(pvt_data.ctx);

	return err;
}

static void optee_pkcs11_remove(struct device *dev)
{
	devinfo_del(dev, optee_pkcs11_devinfo);
	pkcs11_unregister(&pvt_data.optee_cki);

	tee_shm_free(pvt_data.entropy_shm_pool);

	tee_client_close_session(pvt_data.ctx, pvt_data.session_id);
	tee_client_close_context(pvt_data.ctx);
}

static const struct tee_client_device_id optee_pkcs11_id_table[] = {
	{UUID_INIT(0xfd02c9da, 0x306c, 0x48c7, \
		0xa4, 0x9c, 0xbb, 0xd8, 0x27, 0xae, 0x86, 0xee)},
	{}
};

MODULE_DEVICE_TABLE(tee, optee_pkcs11_id_table);

static struct tee_client_driver optee_pkcs11_driver = {
	.id_table	= optee_pkcs11_id_table,
	.driver		= {
		.name		= DRIVER_NAME,
		.bus		= &tee_bus_type,
		.probe		= optee_pkcs11_probe,
		.remove		= optee_pkcs11_remove,
	},
};

static int __init optee_pkcs11_mod_init(void)
{
	return driver_register(&optee_pkcs11_driver.driver);
}
device_initcall(optee_pkcs11_mod_init);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sumit Garg <sumit.garg@linaro.org>");
MODULE_DESCRIPTION("OP-TEE based random number generator driver");
