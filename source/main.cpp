#include <wums.h>
#include <whb/log.h>
#include <whb/log_udp.h>
#include <coreinit/memexpheap.h>
#include "memory_mapping.h"
#include <function_patcher/function_patching.h>
#include "logger.h"
#include "function_replacements.h"

WUMS_MODULE_EXPORT_NAME("homebrew_memorymapping");
WUMS_MODULE_SKIP_ENTRYPOINT();
WUMS_MODULE_INIT_BEFORE_RELOCATION_DONE_HOOK();

WUMS_INITIALIZE(args) {
    static uint8_t ucSetupRequired = 1;
    if (!ucSetupRequired) {
        return;
    }
    ucSetupRequired = 0;
    MemoryMapping_setupMemoryMapping();
    MemoryMapping_CreateHeaps();

    FunctionPatcherPatchFunction(function_replacements, function_replacements_size);
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

void *MemoryMappingAllocEx(uint32_t size, int32_t align) {
    void *res = MemoryMapping_alloc(size, align);
    //DEBUG_FUNCTION_LINE("[res %08X] allocEX %d %d ", res, size, align);
    return res;
}

void *MemoryMappingAllocForGX2Ex(uint32_t size, int32_t align) {
    void *res = MemoryMapping_allocVideoMemory(size, align);
    //DEBUG_FUNCTION_LINE("[res %08X] allocEX %d %d ", res, size, align);
    return res;
}

uint32_t MEMAllocFromMappedMemory __attribute__((__section__ (".data"))) = (uint32_t) MemoryMappingAlloc;
uint32_t MEMAllocFromMappedMemoryEx __attribute__((__section__ (".data"))) = (uint32_t) MemoryMappingAllocEx;
uint32_t MEMAllocFromMappedMemoryForGX2Ex __attribute__((__section__ (".data"))) = (uint32_t) MemoryMappingAllocForGX2Ex;
uint32_t MEMFreeToMappedMemory __attribute__((__section__ (".data"))) = (uint32_t) MemoryMappingFree;

WUMS_EXPORT_FUNCTION(MemoryMappingEffectiveToPhysical);
WUMS_EXPORT_FUNCTION(MemoryMappingPhysicalToEffective);

WUMS_EXPORT_DATA(MEMAllocFromMappedMemory);
WUMS_EXPORT_DATA(MEMAllocFromMappedMemoryEx);
WUMS_EXPORT_DATA(MEMAllocFromMappedMemoryForGX2Ex);
WUMS_EXPORT_DATA(MEMFreeToMappedMemory);
