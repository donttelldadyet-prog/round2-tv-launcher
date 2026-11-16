/* Round2 stand-in for chiaki's common.h: only what rpcrypt.c needs (types copied from chiaki-ng lib/include/chiaki/common.h,
   AGPL-3.0-only with OpenSSL exception). */
#ifndef CHIAKI_COMMON_H
#define CHIAKI_COMMON_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define CHIAKI_EXPORT
typedef enum { CHIAKI_ERR_SUCCESS = 0, CHIAKI_ERR_UNKNOWN, CHIAKI_ERR_INVALID_DATA, CHIAKI_ERR_VERSION_MISMATCH } ChiakiErrorCode;
typedef enum {
	CHIAKI_TARGET_PS4_UNKNOWN =       0,
	CHIAKI_TARGET_PS4_8 =           800,
	CHIAKI_TARGET_PS4_9 =           900,
	CHIAKI_TARGET_PS4_10 =         1000,
	CHIAKI_TARGET_PS5_UNKNOWN = 1000000,
	CHIAKI_TARGET_PS5_1 =       1000100
} ChiakiTarget;
static inline bool chiaki_target_is_ps5(ChiakiTarget target) { return target >= CHIAKI_TARGET_PS5_UNKNOWN; }
#endif
