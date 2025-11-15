// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2018 Linaro Limited
 */

#define pr_fmt(fmt)     "optee-reefs: " fmt

#include <linux/array_size.h>
#include <mci.h>

#include "optee_msg.h"
#include "optee_private.h"

/*
 * Request and response definitions must be in sync with the secure side of
 * OP-TEE.
 */

/* Request */
struct reefs_operation {
	u32 id;
	#define THREAD_RPC_MAX_NUM_PARAMS 4
	struct tee_param params[THREAD_RPC_MAX_NUM_PARAMS];
	size_t num_params;
};

static u32 reefs_process_request(struct tee_context *ctx, void *req,
				ulong req_size, void *rsp, ulong rsp_size)
{
	struct reefs_operation *op = (struct reefs_operation *)req;
	
	pr_info("ReeFS: %d\n", op->id);

	return TEEC_SUCCESS;
}

void optee_suppl_cmd_reefs(struct tee_context *ctx, struct optee_msg_arg *arg)
{
	struct tee_shm *req_shm;
	struct tee_shm *rsp_shm;
	void *req_buf;
	void *rsp_buf;
	ulong req_size;
	ulong rsp_size;

	if (arg->num_params != 2 ||
	    arg->params[0].attr != OPTEE_MSG_ATTR_TYPE_RMEM_INPUT ||
	    arg->params[1].attr != OPTEE_MSG_ATTR_TYPE_RMEM_OUTPUT) {
		arg->ret = TEEC_ERROR_BAD_PARAMETERS;
		return;
	}

	req_shm = (struct tee_shm *)(ulong)arg->params[0].u.rmem.shm_ref;
	req_buf = (u8 *)req_shm->kaddr + arg->params[0].u.rmem.offs;
	req_size = arg->params[0].u.rmem.size;

	rsp_shm = (struct tee_shm *)(ulong)arg->params[1].u.rmem.shm_ref;
	rsp_buf = (u8 *)rsp_shm->kaddr + arg->params[1].u.rmem.offs;
	rsp_size = arg->params[1].u.rmem.size;

	arg->ret = reefs_process_request(ctx, req_buf, req_size,
					rsp_buf, rsp_size);
}
