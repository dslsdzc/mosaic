#ifndef MOSAIC_BASE_H
#define MOSAIC_BASE_H
#include <stdint.h>
#include <string.h>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define MOSAIC_BIG_ENDIAN 1
#else
#define MOSAIC_BIG_ENDIAN 0
#endif

static inline u16 rd_le16(const void *p) { u16 v; memcpy(&v, p, 2); return MOSAIC_BIG_ENDIAN ? (u16)((v >> 8) | (v << 8)) : v; }
static inline u32 rd_le32(const void *p) { u32 v; memcpy(&v, p, 4); return MOSAIC_BIG_ENDIAN ? __builtin_bswap32(v) : v; }
static inline u64 rd_le64(const void *p) { u64 v; memcpy(&v, p, 8); return MOSAIC_BIG_ENDIAN ? __builtin_bswap64(v) : v; }
static inline void wr_le16(void *p, u16 v) { u16 x = MOSAIC_BIG_ENDIAN ? (u16)((v >> 8) | (v << 8)) : v; memcpy(p, &x, 2); }
static inline void wr_le32(void *p, u32 v) { u32 x = MOSAIC_BIG_ENDIAN ? __builtin_bswap32(v) : v; memcpy(p, &x, 4); }
static inline void wr_le64(void *p, u64 v) { u64 x = MOSAIC_BIG_ENDIAN ? __builtin_bswap64(v) : v; memcpy(p, &x, 8); }

enum {
  MOSAIC_OK = 0,
  MOSAIC_ERR_BAD_PACK = 1,   /* 魔数/版本/偏移越界 */
  MOSAIC_ERR_NOT_FOUND = 2,
  MOSAIC_ERR_BUSY = 3,       /* refs > 0 时请求墓碑 */
  MOSAIC_ERR_ILLEGAL = 4,    /* 非法状态转移 */
  MOSAIC_ERR_ABI = 5,        /* dlopen/dlsym/ABI 不匹配 */
  MOSAIC_ERR_NOMEM = 6,
  MOSAIC_ERR_IO = 7,
};
#define MOSAIC_U32_NONE 0xFFFFFFFFu
#endif
