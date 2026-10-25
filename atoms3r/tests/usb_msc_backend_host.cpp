// Compile the *actual* USB MSC callbacks extracted from src/voice.cpp.
// Flash backend is a NOR-erase simulator; no device operations.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cassert>
#include <algorithm>

using wl_handle_t = int32_t;
using esp_err_t = int;
constexpr wl_handle_t WL_INVALID_HANDLE = -1;
constexpr esp_err_t ESP_OK = 0;
struct USBMSC { };
struct FakeSerial {
    template<typename... Args> void printf(const char*, Args...) { }
    void println(const char*) { }
} Serial;

static std::vector<uint8_t> storage;
static int error_read=0,error_write=0,error_erase=0;
static size_t erase_calls=0, write_calls=0;
uint32_t wl_sector_size(wl_handle_t);
size_t wl_size(wl_handle_t);
esp_err_t wl_read(wl_handle_t, size_t, void*, size_t);
esp_err_t wl_write(wl_handle_t, size_t, const void*, size_t);
esp_err_t wl_erase_range(wl_handle_t, size_t, size_t);

// From real source: USBMSC MSC; ... static bool onStartStop(...).
#include "../reports/usb_msc_callbacks.inc"

uint32_t wl_sector_size(wl_handle_t h) { assert(h==123); return s_msc_sector_size; }
size_t wl_size(wl_handle_t h) { assert(h==123); return storage.size(); }
esp_err_t wl_read(wl_handle_t h, size_t p, void* b, size_t n) {
    assert(h==123 && p+n<=storage.size());
    if (error_read) { error_read=0; return -7; }
    memcpy(b, storage.data()+p,n);
    return ESP_OK;
}
esp_err_t wl_erase_range(wl_handle_t h,size_t p,size_t n) {
    assert(h==123 && p+n<=storage.size());
    assert(p % s_msc_sector_size==0 && n==s_msc_sector_size);
    if (error_erase) { error_erase=0; return -9; }
    memset(storage.data()+p,0xff,n); ++erase_calls;
    return ESP_OK;
}
esp_err_t wl_write(wl_handle_t h,size_t p,const void* b,size_t n) {
    assert(h==123 && p+n<=storage.size());
    if (error_write) { error_write=0; return -11; }
    auto* src=reinterpret_cast<const uint8_t*>(b);
    for(size_t i=0;i<n;i++) assert((uint8_t)(storage[p+i] & src[i]) == src[i]);
    memcpy(storage.data()+p,b,n); ++write_calls;
    return ESP_OK;
}
void boot_sector(uint32_t sz) {
    s_wl_handle=123;
    s_msc_sector_size=sz;
    s_msc_block_count=8;
    storage.resize((size_t)sz*8);
    for(size_t i=0;i<storage.size();i++)storage[i]=(uint8_t)((i*7+11)%251);
    erase_calls=write_calls=0;
}
void test(uint32_t sz){
    boot_sector(sz);
    auto before=storage;
    uint8_t a[]={0x33,0x47,0x52,0x1f,0x55};
    assert(onWrite(1,sz-3,a,5)==5);
    assert(erase_calls==2 && write_calls==2);
    for(size_t i=0;i<storage.size();i++){
        uint8_t expected=(i >= (size_t)sz*2-3 && i < (size_t)sz*2+2)
            ? a[i-((size_t)sz*2-3)]:before[i];
        assert(storage[i]==expected);
    }
    uint8_t readback[5]={0};
    assert(onRead(1,sz-3,readback,5)==5 && !memcmp(a,readback,5));
    // One-byte partial writes retain the rest of the WL physical sector.
    uint8_t b=0xba; before=storage;
    assert(onWrite(6,42,&b,1)==1);
    before[(size_t)sz*6+42]=b;
    assert(storage==before);
    // Entire sectors can be replaced without read-modify.
    std::vector<uint8_t> whole(sz,0xa5);
    assert(onWrite(3,0,whole.data(),sz)==(int32_t)sz);
    assert(!memcmp(storage.data()+(size_t)sz*3,whole.data(),sz));
    // Return transport errors instead of false success.
    error_read=1;
    assert(onRead(0,0,readback,5)==-1);
    error_read=1;
    assert(onWrite(1,1,a,5)==-1);
    error_erase=1;
    assert(onWrite(1,1,a,5)==-1);
    error_write=1;
    assert(onWrite(1,1,a,5)==-1);
    assert(onWrite(7,sz-1,a,5)==-1); // physical end overflow
    assert(onRead(8,0,readback,5)==-1); // invalid LBA
    assert(onRead(0,sz,readback,5)==-1); // invalid offset
    assert(onWrite(1,0,nullptr,5)==-1);
    assert(onStartStop(0,true,false));
    printf("PASS USBMSC actual callbacks: WL %u B, partial sectors/cross-boundary, read/write errors and bounds\n",sz);
}
int main(){
    test(512);
    test(4096);
    puts("PASS current full firmware source USB MSC byte preservation and error propagation");
}
