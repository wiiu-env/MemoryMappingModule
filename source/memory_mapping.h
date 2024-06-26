#pragma once

#include <coreinit/memheap.h>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

#include <kernel/kernel.h>
#include <kernel/kernel_defs.h>
#include <stdint.h>

typedef struct pageInformation_ {
    uint32_t addr;
    uint32_t size;
    uint32_t ks;
    uint32_t kp;
    uint32_t nx;
    uint32_t pp;
    uint32_t phys;
} pageInformation;

typedef struct _memory_values_t {
    uint32_t start_address;
    uint32_t end_address;
} memory_values_t;

typedef struct _memory_mapping_t {
    uint32_t effective_start_address;
    uint32_t effective_end_address;
    const memory_values_t *physical_addresses;
} memory_mapping_t;

#define SEGMENT_UNIQUE_ID       0x00AABBCC // Unique ID. Chosen arbitrary.
#define PAGE_INDEX_SHIFT        (32 - 15)
#define PAGE_INDEX_MASK         ((1 << (28 - PAGE_INDEX_SHIFT)) - 1)

#define MEMORY_START_BASE       0x80000000
#define MEMORY_START_VIDEO_BASE (MEMORY_START_BASE + 0x08000000)


// clang-format off
const memory_values_t mem_vals_heap_1[] = {
        {0x28000000 + 0x06620000 + 0x2A0000, 0x28000000 + 0x07F80000}, // size: 23296 kB
        {0,                       0}
};
// clang-format on

#define MEMORY_HEAP0_SIZE       0x16C0000
#define MEMORY_HEAP0            MEMORY_START_BASE

// clang-format off
const memory_values_t mem_vals_heap_2[] = {
        {0x28000000 + 0x09000000, 0x28000000 + 0x09E20000}, // size: 14464 kB
        {0,                       0}
};
// clang-format on

#define MEMORY_HEAP1_SIZE       0xE20000
#define MEMORY_HEAP1            (MEMORY_HEAP0 + MEMORY_HEAP0_SIZE)

// clang-format off
const memory_values_t mem_vals_heap_3[] = {
        {0x28000000 + 0x058E0000, 0x28000000 + 0x06000000}, // size: 7296 kB
        {0,                       0}
};
// clang-format on

#define MEMORY_HEAP2_SIZE       0x720000
#define MEMORY_HEAP2            (MEMORY_HEAP1 + MEMORY_HEAP1_SIZE)

// clang-format off
const memory_values_t mem_vals_heap_4[] = {
        {0x28000000 + 0x053C0000, 0x28000000 + 0x05880000}, // size: 4864 kB
        {0,                       0}
};
// clang-format on

#define MEMORY_HEAP3_SIZE       0x4C0000
#define MEMORY_HEAP3            (MEMORY_HEAP2 + MEMORY_HEAP2_SIZE)

#define MEMORY_HEAP4            (MEMORY_HEAP3 + MEMORY_HEAP3_SIZE)

// clang-format off
const memory_values_t mem_vals_video[] = {
        // The GPU doesn't have access to the 0x28000000 - 0x32000000 area, so we need memory from somewhere else.
        // From the SharedReadHeap of the loader.
        //
        // #define TinyHeap_Alloc ((int32_t (*)(void* heap, int32_t size, int32_t align,void ** outPtr))0x0101235C)
        // #define TinyHeap_Free ((void (*)(void* heap, void * ptr))0x01012814)
        // uint32_t SharedReadHeapTrackingAddr = 0xFA000000 + 0x18 + 0x830 // see https://github.com/decaf-emu/decaf-emu/blob/master/src/libdecaf/src/cafe/loader/cafe_loader_shared.cpp#L490
        //
        // Map the area of the heap to somewhere in the user space and test allocation with
        // void * heap = (void*) SharedReadHeapTrackingAddr - [delta due mapping e.g (0xF8000000 + 0x80000000)];
        // int size = 0x20000; // value have to be a multiple of 0x20000;
        // while(true){
        //     void * outPtr = nullptr;
        //     if(TinyHeap_Alloc(heap,size, 0x20000,&outPtr) == 0){ // value have to be a multiple of 0x20000;
        //         DEBUG_FUNCTION_LINE("Allocated %d kb on heap %08X (PA %08X)\n",size/1024,(uint32_t)outPtr, OSEffectiveToPhysical(outPtr));
        //         TinyHeap_Free(heap, outPtr);
        //     }else{
        //         DEBUG_FUNCTION_LINE("Failed %08X\n",(uint32_t)outPtr);
        //         break;
        //     }
        //     size += 0x20000; // value have to be a multiple of 0x20000;
        // }
        //
        {0x1A020000, 0x1A020000 + 0xE60000},  // size: 14720 kB
        // The following chunk were empty while early tests and are maybe promising. However we can get 15mb from
        // a loader heap. Which should be enough for now.
        //{0x14000000 + 0x02E00000 , 0x14000000 +0x034E0000}, // size: 7040 kB
        //{0x14000000 + 0x02820000 , 0x14000000 +0x02C20000}, // size: 4096 kB
        //{0x14000000 + 0x05AE0000 , 0x14000000 +0x06000000}, // size: 5248 kB
        //{0x14000000 + 0x08040000 , 0x14000000 +0x08400000}, // size: 3840 kB
        //{0x18000000 , 0x18000000 +0x3000000}, // size: 3840 kB
        {0,          0}
};
// clang-format on

#define MEMORY_START_VIDEO      MEMORY_START_VIDEO_BASE
#define MEMORY_END_VIDEO        (MEMORY_START_VIDEO + 0xE60000)

// clang-format off
const memory_mapping_t mem_mapping[] = {
        {MEMORY_HEAP0,       MEMORY_HEAP1,     mem_vals_heap_1},
        {MEMORY_HEAP1,       MEMORY_HEAP2,     mem_vals_heap_2},
        {MEMORY_HEAP2,       MEMORY_HEAP3,     mem_vals_heap_3},
        {MEMORY_HEAP3,       MEMORY_HEAP4,     mem_vals_heap_4},
        {MEMORY_START_VIDEO, MEMORY_END_VIDEO, mem_vals_video},
        {0, 0,                                 nullptr}
};
// clang-format on

// clang-format off
// Values needs to be aligned to 0x20000 and size needs to be a multiple of 0x20000
const memory_values_t mem_vals_heap[] = {
        // 5.5.2 EUR
        {0x28000000 + 0x09000000, 0x28000000 + 0x09E20000}, // size: 14464 kB
        {0x28000000 + 0x058E0000, 0x28000000 + 0x06000000}, // size: 7296 kB
        {0x28000000 + 0x053C0000, 0x28000000 + 0x05880000}, // size: 4864 kB
        //{0x28000000 + 0x08C20000, 0x28000000 + 0x08F20000}, // size: 3072 kB is part of BAT mapping.
        {0x28000000 + 0x00900000, 0x28000000 + 0x00B00000}, // size: 2048 kB

        {0x28000000 + 0x02060000, 0x28000000 + 0x021A0000}, // size: 1280 kB
        {0x28000000 + 0x083C0000, 0x28000000 + 0x084C0000}, // size: 1024 kB
        {0x28000000 + 0x003C0000, 0x28000000 + 0x004C0000}, // size: 1024 kB
        {0x28000000 + 0x02BC0000, 0x28000000 + 0x02CA0000}, // size: 896 kB
        {0x28000000 + 0x080E0000, 0x28000000 + 0x08180000}, // size: 640 kB
        {0x28000000 + 0x000E0000, 0x28000000 + 0x00160000}, // size: 512 kB
        {0x28000000 + 0x00E40000, 0x28000000 + 0x00EC0000}, // size: 512 kB
        {0x28000000 + 0x00EE0000, 0x28000000 + 0x00F60000}, // size: 512 kB
        {0x28000000 + 0x00FA0000, 0x28000000 + 0x01020000}, // size: 512 kB
        {0x28000000 + 0x086E0000, 0x28000000 + 0x08760000}, // size: 512 kB
        {0x28000000 + 0x04B60000, 0x28000000 + 0x04B80000}, // size: 128 kB
        // This chunk was reduced several times, it _might_ be dangerous to use, let's put it right to the end.
        {0x28000000 + 0x01040000, 0x28000000 + 0x01340000}, // size: 3072 kB

        // Not usable on 5.5.2
        //
        // Used in notifications {0x28000000 + 0x01720000, 0x28000000 + 0x018A0000}, // size: 1536 kB
        //                       {0x28000000 + 0x03820000, 0x28000000 + 0x038C0000}, // size: 640 kB
        //                       {0x28000000 + 0x03920000, 0x28000000 + 0x039A0000}, // size: 512 kB
        // Used in notifications {0x28000000 + 0x04B80000, 0x28000000 + 0x051E0000}, // size: 6528 kB
        //                       {0x28000000 + 0x08F20000, 0x28000000 + 0x09000000}, // size: 896 kB

        //                       {0x28000000 + 0x013A0000, 0x28000000 + 0x013C0000}, // size: 128 kB

        // Porting to other/newer firmware:
        // Map this to check for free regions.
        // Use MemoryMapper::testFreeMemory() to see regions with are 0x00000000;
        // Then map the promising regions, and do the write/read check.
        // Writing numbers into the area, open the home menu and all background apps an check if anything was
        // overridden.
        // {0x28000000 + 0x00000000, 0x28000000 + 0x0A000000},              //

        {0,                       0}
};
// clang-format on


uint32_t MemoryMapping_MEMGetAllocatableSize();

uint32_t MemoryMapping_MEMGetAllocatableSizeEx(uint32_t align);

bool MemoryMapping_isMemoryMapped();

void MemoryMapping_setupMemoryMapping();

uint32_t MemoryMapping_GetFreeSpace();

void MemoryMapping_CreateHeaps();

void MemoryMapping_DestroyHeaps();

void MemoryMapping_printPageTableTranslation(sr_table_t srTable, uint32_t *translation_table);

void MemoryMapping_writeTestValuesToMemory();

void MemoryMapping_readTestValuesFromMemory();

void MemoryMapping_searchEmptyMemoryRegions();

void MemoryMapping_checkHeaps();

void *MemoryMapping_alloc(uint32_t size, int32_t align);

void *MemoryMapping_allocVideoMemory(uint32_t size, int32_t align);

void MemoryMapping_free(void *ptr);

MEMHeapHandle MemoryMapping_MEMFindContainHeap(void *block);

uint32_t MemoryMapping_getAreaSizeFromPageTable(uint32_t start, uint32_t maxSize);

uint32_t MemoryMapping_PhysicalToEffective(uint32_t phyiscalAddress);

uint32_t MemoryMapping_EffectiveToPhysical(uint32_t effectiveAddress);

void MemoryMapping_memoryMappingForRegions(const memory_mapping_t *memory_mapping, sr_table_t SRTable, uint32_t *translation_table);

bool MemoryMapping_mapMemory(uint32_t pa_start_address, uint32_t pa_end_address, uint32_t ea_start_address, sr_table_t SRTable, uint32_t *translation_table);

bool MemoryMapping_getPageEntryForAddress(uint32_t SDR1, uint32_t addr, uint32_t vsid, uint32_t *translation_table, uint32_t *oPTEH, uint32_t *oPTEL, bool checkSecondHash);

bool MemoryMapping_getPageEntryForAddressEx(uint32_t pageMask, uint32_t addr, uint32_t vsid, uint32_t primaryHash, uint32_t *translation_table, uint32_t *oPTEH, uint32_t *oPTEL, uint32_t H);

#ifdef __cplusplus
}
#endif