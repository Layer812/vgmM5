#pragma once
// R54 pure image header parser; does not allocate image memory or touch audio.
#include "tab5_cover_art.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace tab5 {
template <typename FileT>
inline bool cover_read_dimensions(FileT& f, CoverFormat format,
                                  uint32_t& w, uint32_t& h) {
  w=0; h=0;
  if (!f || !f.seek(0)) return false;
  auto u8 = [&]() -> int { return f.read(); };
  auto u16be = [&]() -> int {
    const int a=u8(), b=u8();
    return a < 0 || b < 0 ? -1 : (a<<8)|b;
  };
  if (format == CoverFormat::Png) {
    uint8_t header[24];
    if (f.read(header, sizeof(header)) != sizeof(header)) return false;
    static const uint8_t sig[8] = {137,80,78,71,13,10,26,10};
    if (memcmp(header,sig,8) || memcmp(header+12,"IHDR",4)) return false;
    w=((uint32_t)header[16]<<24)|((uint32_t)header[17]<<16)|((uint32_t)header[18]<<8)|header[19];
    h=((uint32_t)header[20]<<24)|((uint32_t)header[21]<<16)|((uint32_t)header[22]<<8)|header[23];
  } else if (format == CoverFormat::Bmp) {
    uint8_t header[26];
    if (f.read(header, sizeof(header)) != sizeof(header) || header[0]!='B' || header[1]!='M') return false;
    const auto le32 = [&](int i) -> uint32_t {
      return (uint32_t)header[i] | ((uint32_t)header[i+1]<<8) |
             ((uint32_t)header[i+2]<<16) | ((uint32_t)header[i+3]<<24);
    };
    w=le32(18); h=le32(22);
  } else if (format == CoverFormat::Jpeg) {
    if (u8()!=0xFF || u8()!=0xD8) return false;
    // Ignore EXIF/ICC/comment segments and read SOF dimensions only.
    // The 128KiB scan bound avoids scanning a malformed multi-megabyte file.
    while (f.position() < f.size() && f.position() < 131072) {
      int prefix=u8();
      if (prefix < 0) break;
      if (prefix != 0xFF) continue;
      int marker;
      do { marker=u8(); } while (marker == 0xFF);
      if (marker < 0 || marker == 0xDA || marker == 0xD9) break;
      if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;
      const int length=u16be();
      const size_t payload_begin=f.position();
      if (length<2 || payload_begin + (size_t)(length-2) > f.size()) break;
      const bool sof=(marker>=0xC0 && marker<=0xCF &&
                      marker!=0xC4 && marker!=0xC8 && marker!=0xCC);
      if (sof && length>=7) {
        if (u8()<0) break; // precision
        const int height=u16be(), width=u16be();
        if (height>0 && width>0) {w=(uint32_t)width; h=(uint32_t)height; break;}
      }
      if (!f.seek(payload_begin + (size_t)(length-2))) break;
    }
  }
  // Bound decode cost; UI must not starve the real-time audio task.
  return w>0 && h>0 && w<=4096 && h<=4096 && (uint64_t)w*h<=12000000u;
}
} // namespace tab5
