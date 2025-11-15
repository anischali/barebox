// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <command.h>
#include <getopt.h>
#include <libfile.h>
#include <crypto/keystore.h>
#include <linux/kernel.h>
#include <tee/tk.h>
#include <base64.h>
#include <fs.h>

static char *to_base64(const char *in, size_t len)
{
    char *value;

    value = malloc(BASE64_LENGTH(len) + 1);
	if (!value)
		return NULL;

    uuencode(value, in, len);

    return value;
}

static int do_trusted_keys(int argc, char *argv[])
{
	int opt;
	int ret;
	char key[512], tmp[512], *s_key;
    char *val64;
	int rnd_key = 0, seal = 0, unseal = 0, rnd = 0;
	const char *file = NULL;
    size_t s_len, len = 512;

	while ((opt = getopt(argc, argv, "u:s:r:f:")) > 0) {
		switch (opt) {
		case 'r':
            rnd = 1;
            printf("tk:  %s\n", optarg);
			ret = kstrtouint(optarg, 10, &rnd_key);
            if (ret || rnd_key % 8 != 0) {
                printf("key len bits need to an 8 divider %d\n", rnd_key);
			    return 1;
            }
            rnd_key /= 8;
			break;
		case 's':
			seal = 1;
            s_key = optarg;
            s_len = strlen(s_key);
			break;
        case 'u':
			unseal = 1;
            s_key = optarg;
            s_len = strlen(s_key);
			break;
		case 'f':
			file = optarg;
			break;
		default:
			return COMMAND_ERROR_USAGE;
		}
	}

	if (rnd) {
        tk_get_random(key, rnd_key);
		//hexdump("random key", key, rnd_key);
        return 0;
	}

    if (seal) {
        val64 = to_base64(s_key, s_len);
        pr_info("Unsealed base64 %s\n", val64);
        tk_seal(val64, BASE64_LENGTH(s_len) + 1, key, &len);
        free(val64);
        memset(tmp, 0x0, 512);
        memcpy(tmp, key, len);
        s_len = len;
        val64 = to_base64(key, len);
        pr_info("Sealed base64 (%lu|%lu): %s\n", len, BASE64_LENGTH(len) + 1, val64);
        free(val64);
        memset(key, 0x0, 512);
        len = 0;
        tk_unseal(tmp, s_len, key, &len);
        val64 = to_base64(key, len);
        pr_info("Unsealed base64 (%lu|%lu): %s\n", len, BASE64_LENGTH(len) + 1, val64);
        free(val64);
        return 0;
    }

    if (unseal) {
        tk_unseal(s_key, s_len, key, &len);
        //hexdump("Sealed", s_key, s_len);
        //hexdump("Unsealed", key, len);
        return 0;
    }

	return ret ? 1 : 0;
}

BAREBOX_CMD_HELP_START(tk)
BAREBOX_CMD_HELP_TEXT("")
BAREBOX_CMD_HELP_TEXT("Options:")
BAREBOX_CMD_HELP_OPT("-r <bitlen>", "get random bytes of len.")
BAREBOX_CMD_HELP_OPT("-s <key>", "set a key in the keystore")
BAREBOX_CMD_HELP_OPT("-u <key>", "set a key in the keystore")
BAREBOX_CMD_HELP_OPT("-f <keyfile>", "set a sealed key to a file, unseal a key from a file")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(tk)
	.cmd	= do_trusted_keys,
	BAREBOX_CMD_DESC("trusted keys seal/unseal")
	BAREBOX_CMD_OPTS("[-rsuf] <key>")
	BAREBOX_CMD_GROUP(CMD_GRP_SECURITY)
	BAREBOX_CMD_HELP(cmd_tk_help)
BAREBOX_CMD_END
