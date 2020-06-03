#include <coreinit/debug.h>
#include <cstddef>
#include <malloc.h>
#include <wums.h>
#include <whb/log.h>
#include <whb/log_udp.h>
#include "memory_mapping.h"
#include "logger.h"

WUMS_MODULE_EXPORT_NAME("homebrew_memorymapping");

WUMS_INITIALIZE() {
    WHBLogUdpInit();
    DEBUG_FUNCTION_LINE("Setting up memory mapping!");
    static uint8_t ucSetupRequired = 1;
    if (!ucSetupRequired) {
        return;
    }
    ucSetupRequired = 0;
    MemoryMapping_setupMemoryMapping();
    MemoryMapping_CreateHeaps();
    DEBUG_FUNCTION_LINE("total free space %d KiB", MemoryMapping_GetFreeSpace() / 1024);
}

WUMS_APPLICATION_STARTS() {
    WHBLogUdpInit();
    //MemoryMapping_DestroyHeaps();
    //MemoryMapping_CreateHeaps();
    DEBUG_FUNCTION_LINE("total free space %d KiB", MemoryMapping_GetFreeSpace() / 1024);
}

void MemoryMappingFree(void *ptr) {
    //DEBUG_FUNCTION_LINE("[%08X] free", ptr);
    MemoryMapping_free(ptr);
}

uint32_t MemoryMappingEffectiveToPhysical(uint32_t address) {
    return MemoryMapping_EffectiveToPhysical(address);
}

uint32_t MemoryMappingPhysicalToEffective(uint32_t address) {
    return MemoryMapping_PhysicalToEffective(address);
}

void *MemoryMappingAlloc(uint32_t size) {
    void *res = MemoryMapping_alloc(size, 0x04);
    //DEBUG_FUNCTION_LINE("[res: %08X] alloc %d ", res, size);
    return res;
}

void *MemoryMappingAllocEx(uint32_t size, uint32_t align) {
    void *res = MemoryMapping_alloc(size, align);
    //DEBUG_FUNCTION_LINE("[res %08X] allocEX %d %d ", res, size, align);
    return res;
}

uint32_t MEMAllocFromMappedMemory __attribute__((__section__ (".data"))) = (uint32_t) MemoryMappingAlloc;
uint32_t MEMAllocFromMappedMemoryEx __attribute__((__section__ (".data"))) = (uint32_t) MemoryMappingAllocEx;
uint32_t MEMFreeToMappedMemory __attribute__((__section__ (".data"))) = (uint32_t) MemoryMappingFree;

WUMS_EXPORT_FUNCTION(MemoryMappingEffectiveToPhysical);
WUMS_EXPORT_FUNCTION(MemoryMappingPhysicalToEffective);

WUMS_EXPORT_DATA(MEMAllocFromMappedMemory);
WUMS_EXPORT_DATA(MEMAllocFromMappedMemoryEx);
WUMS_EXPORT_DATA(MEMFreeToMappedMemory);
