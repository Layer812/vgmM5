#ifndef _LZX_H_
#define _LZX_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// LZX圧縮されているか判定し、解凍後のサイズを取得する
// 圧縮されている場合はtrueを返し、decomp_sizeにサイズを格納する
bool isLZXCompressed(const uint8_t *data, int len, uint32_t *decomp_size);

// LZX圧縮データを解凍する
// 成功した場合はmallocされたバッファへのポインタを返し、out_lenに解凍後のサイズを格納する
// 失敗した場合はNULLを返す
// 呼び出し元で不要になったらfree()すること
uint8_t* LZXDecode(const uint8_t *src, int src_len, uint32_t *out_len);

#ifdef __cplusplus
}
#endif

#endif // _LZX_H_
