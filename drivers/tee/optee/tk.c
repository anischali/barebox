#include <linux/types.h>
#include <tee/tk.h>
#include <linux/tee_drv.h>

#include "optee_private.h"

#define TA_TK_UUID UUID_INIT(0xf04a0fe7, 0x1f5d, 0x4b9b, \
					  0xab, 0xf7, 0x61, 0x9b, 0x85, 0xb4, 0xce, 0x8c)

#define TA_TK_CMD_GET_RANDOM	0x0
#define TA_TK_CMD_SEAL			0x1
#define TA_TK_CMD_UNSEAL		0x2

#define TEE_PARAM_TYPE_MEMREF_INPUT		5
#define TEE_PARAM_TYPE_MEMREF_OUTPUT	6

struct tk_optee {
	
};

static int optee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	if (ver->impl_id == TEE_IMPL_ID_OPTEE)
		return 1;
	else
		return 0;
}

int tk_get_random(u8 *buf, size_t size) {
	const uuid_t tk_uuid = TA_TK_UUID;
	int rc = 0;
	struct tee_shm *shm_buf;
	struct tee_param param[2];
	struct tee_ioctl_open_session_arg sess_arg = {};
	struct tee_context *ctx = NULL;
	struct tee_ioctl_invoke_arg arg;

	ctx = tee_client_open_context(NULL, optee_ctx_match, NULL, NULL);
	if (IS_ERR(ctx))
		return -ENODEV;

	export_uuid(sess_arg.uuid, &tk_uuid);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_REE_KERNEL;
	sess_arg.num_params = 0;

	rc = tee_client_open_session(ctx, &sess_arg, NULL);
	if ((rc < 0) || (sess_arg.ret != TEEC_SUCCESS)) {
		pr_debug("%s device enumeration pseudo TA not found\n", __func__);
		rc = 0;
		goto out_ctx;
	}

	shm_buf = tee_shm_alloc_kernel_buf(ctx, size);
	if (IS_ERR(shm_buf)) {
		rc = -ENOMEM;
		goto close_session;
	}

	memset(param, 0, sizeof(param));
	param[0].attr = TEE_PARAM_TYPE_MEMREF_OUTPUT;
	param[0].u.memref.shm = shm_buf;
	param[0].u.memref.size = size;

	arg.func = TA_TK_CMD_GET_RANDOM;
	arg.session = sess_arg.session;
	arg.num_params = 1;

	rc = tee_client_invoke_func(ctx, &arg, param);
	if (rc)
		goto out;
	switch (arg.ret) {
	case TEEC_SUCCESS:
		rc = 0;
		break;
	case TEEC_ERROR_ITEM_NOT_FOUND:
		rc = -ENOENT;
		break;
	default:
		rc = -EINVAL;
		break;
	}
	if (rc)
		goto out;

	if (param[1].u.memref.size > size) {
		rc = -EINVAL;
		goto out;
	}

	memcpy(buf, shm_buf->kaddr, size);

out:
	tee_shm_free(shm_buf);
close_session:
	tee_client_close_session(ctx, sess_arg.session);
out_ctx:
	tee_client_close_context(ctx);

	return rc;
}

static int tk_op(u8 *in_buf, size_t in_size, u8 *out_buf,
				  size_t *out_size, unsigned int op)
{
	const uuid_t tk_uuid = TA_TK_UUID;
	int rc = 0;
	struct tee_shm *shm_buf, *shm_outbuf;
	struct tee_param param[2];
	struct tee_ioctl_open_session_arg sess_arg = {};
	struct tee_context *ctx = NULL;
	struct tee_ioctl_invoke_arg arg;

	ctx = tee_client_open_context(NULL, optee_ctx_match, NULL, NULL);
	if (IS_ERR(ctx))
		return -ENODEV;

	export_uuid(sess_arg.uuid, &tk_uuid);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_REE_KERNEL;
	sess_arg.num_params = 0;

	rc = tee_client_open_session(ctx, &sess_arg, NULL);
	if ((rc < 0) || (sess_arg.ret != TEEC_SUCCESS)) {
		pr_debug("%s device enumeration pseudo TA not found\n", __func__);
		rc = 0;
		goto out_ctx;
	}

	shm_buf = tee_shm_alloc_kernel_buf(ctx, in_size);
	if (IS_ERR(shm_buf)) {
		rc = -ENOMEM;
		goto close_session;
	}

	shm_outbuf = tee_shm_alloc_kernel_buf(ctx, 512);
	if (IS_ERR(shm_outbuf)) {
		rc = -ENOMEM;
		goto free_shm_buf;
	}

	memset(param, 0, sizeof(param));
	param[0].attr = TEE_PARAM_TYPE_MEMREF_INPUT;
	param[0].u.memref.shm = shm_buf;
	param[0].u.memref.size = in_size;

	param[1].attr = TEE_PARAM_TYPE_MEMREF_OUTPUT;
	param[1].u.memref.shm = shm_outbuf;
	param[1].u.memref.size = 512;

	arg.func = op;
	arg.session = sess_arg.session;
	arg.num_params = 2;

	rc = tee_client_invoke_func(ctx, &arg, param);
	if (rc)
		goto out;
	switch (arg.ret) {
	case TEEC_SUCCESS:
		rc = 0;
		break;
	case TEEC_ERROR_ITEM_NOT_FOUND:
		rc = -ENOENT;
		break;
	default:
		rc = -EINVAL;
		break;
	}
	if (rc)
		goto out;

	if (param[1].u.memref.size > 512) {
		rc = -EINVAL;
		goto out;
	}

	*out_size = param[1].u.memref.size;
	memcpy(out_buf, shm_outbuf->kaddr, *out_size);

out:
	tee_shm_free(shm_outbuf);
free_shm_buf:
	tee_shm_free(shm_buf);
close_session:
	tee_client_close_session(ctx, sess_arg.session);
out_ctx:
	tee_client_close_context(ctx);

	return rc;
}

int tk_seal(u8 *in_buf, size_t in_size,
			      u8 *sealed, size_t *out_size)
{
	return tk_op(in_buf, in_size, sealed, 
			out_size, TA_TK_CMD_SEAL);
}

int tk_unseal(u8 *in_buf, size_t in_size,
			      u8 *unsealed, size_t *out_size)
{
	return tk_op(in_buf, in_size, unsealed, 
			out_size, TA_TK_CMD_UNSEAL);
}
