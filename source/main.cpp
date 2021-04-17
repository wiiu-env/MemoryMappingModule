#include <coreinit/debug.h>
#include <cstddef>
#include <malloc.h>
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
WUMS_MODULE_SKIP_WUT_INIT();
WUMS_MODULE_INIT_BEFORE_RELOCATION_DONE_HOOK();

WUMS_INITIALIZE(args)  {
    // Currently we have no logging because we're skipping the wut init/fini calls.
    // WHBLogUdpInit();
    DEBUG_FUNCTION_LINE("Setting up memory mapping!");
    static uint8_t ucSetupRequired = 1;
    if (!ucSetupRequired) {
        return;
    }
    ucSetupRequired = 0;
    MemoryMapping_setupMemoryMapping();
    MemoryMapping_CreateHeaps();
    DEBUG_FUNCTION_LINE_VERBOSE("Memory Mapping: Total free space %d KiB", MemoryMapping_GetFreeSpace() / 1024);

    DEBUG_FUNCTION_LINE("Patch functions");
    FunctionPatcherPatchFunction(function_replacements, function_replacements_size);
    DEBUG_FUNCTION_LINE("Patch functions finished");
}

WUMS_APPLICATION_STARTS() {
    // Currently we have no logging because we're skipping the wut init/fini calls.
    // WHBLogUdpInit();
    //MemoryMapping_DestroyHeaps();
    //MemoryMapping_CreateHeaps();

    for (int32_t i = 0; /* waiting for a break */; i++) {
        if (mem_mapping[i].physical_addresses == NULL) {
            break;
        }
        void *address = (void *) (mem_mapping[i].effective_start_address);

        MEMExpHeapBlock *curUsedBlock = ((MEMExpHeap *) address)->usedList.head;
        while (curUsedBlock != nullptr) {
            DEBUG_FUNCTION_LINE_VERBOSE("[Memory leak info] %08X is still allocated (%d bytes)", &curUsedBlock[1], curUsedBlock->blockSize);
            curUsedBlock = curUsedBlock->next;
        }
    }

    DEBUG_FUNCTION_LINE("Memory Mapping: Current free space %d KiB", MemoryMapping_GetFreeSpace() / 1024);
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

void *MemoryMappingAllocForGX2Ex(uint32_t size, uint32_t align) {
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
