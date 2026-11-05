// PR55: verify every VGM byte/command position is identical with and
// without the 1KiB internal-DRAM staging window. Not a CPU benchmark;
// host RAM performance does NOT model ESP32-S3 PSRAM/Flash contention.
#include "../r67_pr55_vgm_command_cache.hpp"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <inttypes.h>

static uint32_t rng=0x84de2937u;
static uint32_t random32() {
    rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5;
    return rng;
}
static uint64_t add_hash(uint64_t hash, uint32_t value) {
    return (hash ^ value) * UINT64_C(1099511628211);
}
struct Reader {
    const uint8_t* data;
    uint32_t length;
    uint32_t pos=0;
    bool staging;
    CoreS3VgmCommandCache<1024> cache={};
    uint32_t cache_misses=0;
    Reader(const uint8_t* input,uint32_t size,bool cached)
        : data(input),length(size),staging(cached) {}
    uint8_t take() {
        assert(pos<length);
        const uint32_t at=pos++;
        if (!staging) return data[at];
        if (at < cache.first || at-cache.first >= cache.count) {
            ++cache_misses;
        }
        return cache.read(at,data,length);
    }
    uint8_t take_or_end() {
        if (pos>=length) return 0x66;
        return take();
    }
    uint32_t le32() {
        uint32_t v=0;
        for(int i=0;i<4;i++)v |= (uint32_t)take() << (i*8);
        return v;
    }
};
struct Trace {
    uint64_t hash;
    uint32_t commands;
    uint32_t fm;
    uint32_t pcm;
    uint32_t blocks;
    uint32_t loops;
    uint32_t final_pos;
};
static Trace parse(const uint8_t* data,uint32_t length,uint32_t first,
                   uint32_t loop_addr,bool cached) {
    Reader reader(data,length,cached);
    reader.pos=first;
    Trace t={UINT64_C(14695981039346656037),0,0,0,0,0,0};
    for(uint32_t i=0;i<5000000u;i++) {
        if (reader.pos>=length) break;
        const uint32_t old_pos=reader.pos;
        const uint8_t c=reader.take();
        t.hash=add_hash(t.hash,c);
        t.hash=add_hash(t.hash,old_pos);
        ++t.commands;
        if(c==0x67) {
            assert(reader.take()==0x66);
            assert(reader.take()==0x8D);
            const uint32_t n=reader.le32();
            assert(reader.pos+n<=length);
            reader.pos+=n;   // payload skip: zero PCM bytes read by decoder
            ++t.blocks;
        } else if(c==0x54) {
            const uint8_t reg=reader.take(),val=reader.take();
            t.hash=add_hash(add_hash(t.hash,reg),val);
            ++t.fm;
        } else if(c==0xD4) {
            const uint8_t port=reader.take(),reg=reader.take(),val=reader.take();
            t.hash=add_hash(add_hash(add_hash(t.hash,port),reg),val);
            ++t.pcm;
        } else if(c==0x61) {
            const uint8_t lo=reader.take(),hi=reader.take();
            t.hash=add_hash(t.hash,lo+((uint32_t)hi<<8));
        } else if(c>=0x70 && c<=0x7F) {
            t.hash=add_hash(t.hash,c & 15u);
        } else if(c==0x66) {
            if(t.loops==2u) break;
            reader.pos=loop_addr;
            ++t.loops;
        } else {
            fprintf(stderr,"unexpected command %02x at %u\n",c,old_pos);
            assert(false);
        }
    }
    t.final_pos=reader.pos;
    return t;
}
static std::vector<uint8_t> generate(uint32_t& loop_addr,uint32_t& end_addr) {
    // Match the actual Dragon Saber VGM's 1.2MB layout / nine
    // 0x67/0x8D ROM blocks, then synthesize a long write-command stream.
    std::vector<uint8_t> img(1202448u,0);
    uint32_t pos=192;
    auto put=[&](uint8_t b){assert(pos<img.size());img[pos++]=b;};
    auto little32=[&](uint32_t n){for(int i=0;i<4;i++)put(n>>(8*i));};
    const uint32_t sizes[]={45000,14149,23688,9608,20200,29444,64024,42868,324};
    for (auto size:sizes) {
        put(0x67);put(0x66);put(0x8D);little32(size);
        for (uint32_t i=0;i<size;i++)put(static_cast<uint8_t>(random32()));
    }
    loop_addr=pos;
    // 183,123 writes spread across a real-length command stream.
    for(uint32_t i=0;i<183123;i++){
        if (i%5u==0) {
            put(0x54);put(random32());put(random32());
        } else {
            put(0xD4);put(random32());put(random32());put(random32());
        }
        if(i%2u==0) put(0x70u+((i*7u)%16u));
        if(i%337u==0) {
            put(0x61);put(0x01);put(0x00);
        }
    }
    put(0x66);
    end_addr=pos;
    return img;
}
int main() {
    uint32_t loop_addr=0,end_addr=0;
    const auto img=generate(loop_addr,end_addr);
    const auto a=parse(img.data(),img.size(),192,loop_addr,false);
    const auto b=parse(img.data(),img.size(),192,loop_addr,true);
    assert(memcmp(&a,&b,sizeof(a))==0);
    assert(a.blocks==9u && a.loops==2u && a.fm>100000u && a.pcm>400000u);
    // Test every byte of the backing VGM, including page boundaries and
    // EOF, as though it were one long sequential command stream.
    Reader uncached(img.data(),img.size(),false);
    Reader cached(img.data(),img.size(),true);
    uint64_t bytes_hash=UINT64_C(14695981039346656037);
    for(uint32_t i=0;i<img.size();i++) {
        const auto x=uncached.take();
        const auto y=cached.take();
        assert(x==y);
        bytes_hash=add_hash(bytes_hash,y);
    }
    assert(uncached.take_or_end()==0x66 && cached.take_or_end()==0x66);
    // Read after non-monotonic VGM seeks and skipped PCM chunks.
    for(unsigned i=0;i<100000u;i++) {
        const uint32_t offset=random32()%img.size();
        uncached.pos=offset;cached.pos=offset;
        const unsigned count=1+(random32()%7u);
        for(unsigned j=0;j<count && uncached.pos<img.size();j++)
            assert(uncached.take()==cached.take());
    }
    // New-song reset must invalidate identical offsets from old songs.
    auto second=img;
    for(unsigned i=192;i<1199;i++)second[i]^=0xA5;
    cached.data=second.data();cached.pos=192;cached.cache.reset();
    uncached.data=second.data();uncached.pos=192;
    for(unsigned i=0;i<2000;i++)
        assert(uncached.take()==cached.take());
    printf("PR55_COMMAND_HASH=%016" PRIx64 " writes=%u blocks=%u loops=%u\n",
           a.hash,a.fm+a.pcm,a.blocks,a.loops);
    printf("PR55_BYTE_HASH=%016" PRIx64 " bytes=%zu\n",bytes_hash,img.size());
    puts("PASS VGM cache: byte-for-byte identical commands, offsets, 9 PCM skips, 2 loops, random seeks, 1.2MB linear scan, new-song reset");
    return 0;
}
