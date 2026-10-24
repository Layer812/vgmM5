#include "lzx.h"
#include <stdlib.h>
#include <string.h>

bool isLZXCompressed(const uint8_t *data, int len, uint32_t *decomp_size) {
    if (len < 14) return false;
    
    // Skip title
    int i = 0;
    for(i = 2; i < len; i++) {
        if(data[i] == 0x1a && data[i-1] == 0x0a && data[i-2] == 0x0d) {
            i++;
            break;
        }
    }
    if (i >= len) return false;
    
    // Skip PDX filename
    for(; i < len; i++) {
        if(data[i] == 0) {
            i++;
            break;
        }
    }
    if (i >= len) return false;
    
    int offsetstart = i;
    if (offsetstart + 14 > len) return false;
    
    if (data[offsetstart + 4] == 'L' && data[offsetstart + 5] == 'Z' && data[offsetstart + 6] == 'X' && data[offsetstart + 7] == ' ') {
        // decompressed size is likely at offsetstart + 14 or offsetstart + 18
        // Let's read from offsetstart + 14 based on original math: (pos=8) - 4 + 4 + 6 = 14
        *decomp_size = (data[offsetstart + 14] << 24) | (data[offsetstart + 15] << 16) | (data[offsetstart + 16] << 8) | data[offsetstart + 17];
        return true;
    }
    return false;
}

typedef struct {
    int last;
    uint8_t data;
} CBitReader;

static inline bool GetBitBool(CBitReader *br, const uint8_t **src, const uint8_t *end) {
    if (br->last == 0) {
        if (*src < end) {
            br->data = *(*src)++;
        } else {
            br->data = 0;
        }
        br->last = 8;
    }
    br->last--;
    bool res = (br->data & 0x80) != 0;
    br->data <<= 1;
    return res;
}

static inline int GetBitInt(CBitReader *br, const uint8_t **src, const uint8_t *end) {
    return GetBitBool(br, src, end) ? 1 : 0;
}

static inline int32_t ReadS32(const uint8_t **src, const uint8_t *end) {
    uint8_t high = (*src < end) ? *(*src)++ : 0;
    uint8_t low = (*src < end) ? *(*src)++ : 0;
    return (int32_t)(0xFFFF0000 | (high << 8) | low);
}

uint8_t* LZXDecode(const uint8_t *src, int src_len, uint32_t *out_len) {
    uint32_t decompsize = 0;
    if (!isLZXCompressed(src, src_len, &decompsize)) {
        return NULL;
    }
    if (decompsize == 0 || decompsize > 1024 * 1024) { // Max 1MB sanity check
        return NULL;
    }

    int offsetstart = 0;
    // Skip title
    int i = 0;
    for(i = 2; i < src_len; i++) {
        if(src[i] == 0x1a && src[i-1] == 0x0a && src[i-2] == 0x0d) {
            i++;
            break;
        }
    }
    if (i < src_len) {
        // Skip PDX filename
        for(; i < src_len; i++) {
            if(src[i] == 0) {
                i++;
                break;
            }
        }
        if (i < src_len) {
            offsetstart = i;
        }
    }

    uint8_t *wbuf = (uint8_t*)malloc(offsetstart + decompsize + 16);
    if (!wbuf) {
        return NULL;
    }
    
    // Copy the uncompressed MDX header (title and PDX filename)
    if (offsetstart > 0) {
        memcpy(wbuf, src, offsetstart);
    }

    const uint8_t *end = src + src_len;
    
    // Search for ID 0x7FFFFF4C starting from offsetstart
    const uint8_t *search_src = src + offsetstart;
    bool found = false;
    while (search_src + 4 <= end) {
        uint32_t ID = (search_src[0] << 24) | (search_src[1] << 16) | (search_src[2] << 8) | search_src[3];
        if (ID == 0x7FFFFF4C) {
            search_src += 4;
            found = true;
            break;
        }
        search_src += 2;
    }

    if (!found) {
        free(wbuf);
        return NULL;
    }

    CBitReader br = {0, 0};
    int wbufidx = offsetstart; // Start decompressing AFTER the header

    while (search_src < end) {
        // L26
        if (GetBitBool(&br, &search_src, end)) {
            if (search_src < end && wbufidx < offsetstart + decompsize + 16) {
                wbuf[wbufidx++] = *search_src++;
            } else {
                break;
            }
            continue;
        }

        // L36
        if (!GetBitBool(&br, &search_src, end)) {
            int CL = GetBitInt(&br, &search_src, end);
            CL = (CL << 1) + GetBitInt(&br, &search_src, end) + 2;

            if (search_src >= end) break;
            int ALs8 = (int)(*search_src++) - 256;
            int CopyFromPos = wbufidx + ALs8;
            
            // Cannot copy from BEFORE offsetstart!
            if (CopyFromPos < offsetstart || wbufidx + CL > offsetstart + decompsize + 16) break;
            
            for (int idx = 0; idx < CL; idx++) {
                wbuf[wbufidx++] = wbuf[CopyFromPos++];
            }
            continue;
        } else {
            if (search_src + 2 > end) break;
            int32_t AX = ReadS32(&search_src, end);
            int CL = AX & 7;
            AX >>= 3; // Arithmetic shift
            
            if (CL != 0) {
                CL += 2;
                int CopyFromPos = wbufidx + AX;
                if (CopyFromPos < offsetstart || wbufidx + CL > offsetstart + decompsize + 16) break;
                for (int idx = 0; idx < CL; idx++) {
                    wbuf[wbufidx++] = wbuf[CopyFromPos++];
                }
                continue;
            }

            if (search_src >= end) break;
            CL = *search_src++;
            if (CL != 0) {
                CL++;
                int CopyFromPos = wbufidx + AX;
                if (CopyFromPos < offsetstart || wbufidx + CL > offsetstart + decompsize + 16) break;
                for (int idx = 0; idx < CL; idx++) {
                    wbuf[wbufidx++] = wbuf[CopyFromPos++];
                }
                continue;
            }

            if ((uint32_t)(wbufidx - offsetstart) != decompsize) {
                free(wbuf);
                return NULL;
            }

            *out_len = offsetstart + decompsize;
            return wbuf;
        }
    }

    free(wbuf);
    return NULL;
}
