#define main secureboot_runner_main
#include "secureboot.c"
#undef main

int main(void)
{
    unsigned char volume[1024] = {0};
    memcpy(volume + 16, nv_guid, 16);
    put_le(volume + 32, sizeof(volume), 8);
    put_le(volume + 40, 0x4856465f, 4);
    put_le(volume + 48, 72, 2);
    
    memcpy(volume + 72, auth_guid, 16);
    
    put_le(volume + 88, 952, 4);
    
    volume[92] = 0x5a;
    volume[93] = 0xfe;
    
    memset(volume + 100, 0xff, sizeof(volume) - 100);
    
    struct store store;
    
    for (size_t size = 0; size < sizeof(volume); ++size) {
        CHECK(store_open(&store, volume, size) != 0);
    }
    
    CHECK(store_open(&store, volume, sizeof(volume)) == 0);
    
    const unsigned char one = 1;
    
    CHECK(add_variable(&store, "SecureBootEnable", enable_guid, 3, &one, 1) == 0);
    CHECK(get16(volume + 100) == 0x55aa && volume[102] == 0x3f);
    CHECK(get32(volume + 104) == 3 && get32(volume + 136) == 34);
    CHECK(get32(volume + 140) == 1 && volume[194] == 1 && store.cursor == 196);
    CHECK(memcmp(volume + 144, enable_guid, 16) == 0);
    CHECK(store_open(&store, volume, sizeof(volume)) != 0);
    
    store.cursor = store.end - 8;
    
    CHECK(add_variable(&store, "PK", global_guid, 0x27, &one, 1) != 0);
    
    memset(volume + 100, 0xff, sizeof(volume) - 100);
    
    volume[72] ^= 1;
    
    CHECK(store_open(&store, volume, sizeof(volume)) != 0);
    
    volume[72] ^= 1;
    
    put_le(volume + 88, UINT32_MAX, 4);
    
    CHECK(store_open(&store, volume, sizeof(volume)) != 0);

    unsigned char image[512] = {0};
    
    memcpy(image, "MZ", 2);
    memcpy(image + 64, "PE\0\0", 4);
    
    put_le(image + 60, 64, 4);
    put_le(image + 68, 0x8664, 2);
    put_le(image + 70, 1, 2);
    put_le(image + 84, 240, 2);
    put_le(image + 88, 0x20b, 2);
    put_le(image + 112, 0x10000000, 8);
    put_le(image + 120, 4096, 4);
    
    memcpy(image + 328, ".linux", 6);
    
    put_le(image + 336, 1, 4);
    put_le(image + 340, 4096, 4);
    put_le(image + 344, 1, 4);
    put_le(image + 348, 511, 4);
    
    struct pe pe;
    
    for (size_t size = 0; size < sizeof(image); ++size) {
        CHECK(parse_pe(&pe, image, size) != 0);
    }
    
    CHECK(parse_pe(&pe, image, sizeof(image)) == 0);
    CHECK(pe.base == 0x10000000 && pe.align == 4096 && pe.end == 4097);
    
    put_le(image + 348, UINT32_MAX, 4);
    
    CHECK(parse_pe(&pe, image, sizeof(image)) != 0);
    
    put_le(image + 348, 511, 4);
    put_le(image + 120, 3, 4);
    
    CHECK(parse_pe(&pe, image, sizeof(image)) != 0);
    
    put_le(image + 120, 4096, 4);
    put_le(image + 60, UINT32_MAX, 4);
    
    CHECK(parse_pe(&pe, image, sizeof(image)) != 0);
    
    puts("Secure Boot format bounds, pristine-store checks, and PE layout tests passed.");
    return(0);
}
