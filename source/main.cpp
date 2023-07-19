#include "function_replacements.h"
#include "globals.h"
#include "logger.h"
#include "memory_mapping.h"
#include "version.h"
#include <coreinit/debug.h>
#include <function_patcher/function_patching.h>
#include <wums.h>
#ifdef DEBUG
#include "logger.h"
#endif

#define VERSION "v0.2.5"

WUMS_MODULE_EXPORT_NAME("homebrew_memorymapping");
WUMS_MODULE_SKIP_INIT_FINI();
WUMS_MODULE_INIT_BEFORE_RELOCATION_DONE_HOOK();
WUMS_DEPENDS_ON(homebrew_kernel);
WUMS_DEPENDS_ON(homebrew_functionpatcher);

#include <coreinit/dynload.h>
#include <coreinit/memdefaultheap.h>

// We can't use the functions from libfunctionpatcher. Defined in functionpatcher.def
extern "C" FunctionPatcherStatus FPAddFunctionPatch(function_replacement_data_t *function_data, PatchedFunctionHandle *outHandle, bool *outHasBeenPatched);

void UpdateFunctionPointer() {
    // We need the real MEMAllocFromDefaultHeapEx/MEMFreeToDefaultHeap function pointer to force-allocate memory on the default heap.
    // Our custom heap doesn't work (yet) for threads and causes an app panic.
    OSDynLoad_Module coreinitModule;
    if (OSDynLoad_Acquire("coreinit", &coreinitModule) != OS_DYNLOAD_OK) {
        DEBUG_FUNCTION_LINE_ERR("Failed to acquire coreinit.rpl");
        OSFatal("FunctionPatcherModule: Failed to acquire coreinit.rpl");
    }
    /* Memory allocation functions */
    uint32_t *allocPtr, *freePtr;
    if (OSDynLoad_FindExport(coreinitModule, OS_DYNLOAD_EXPORT_DATA, "MEMAllocFromDefaultHeapEx", reinterpret_cast<void **>(&allocPtr)) != OS_DYNLOAD_OK) {
        DEBUG_FUNCTION_LINE_ERR("OSDynLoad_FindExport for MEMAllocFromDefaultHeapEx");
        OSFatal("MemoryMappingModule: OSDynLoad_FindExport for MEMAllocFromDefaultHeapEx");
    }
    if (OSDynLoad_FindExport(coreinitModule, OS_DYNLOAD_EXPORT_DATA, "MEMFreeToDefaultHeap", reinterpret_cast<void **>(&freePtr)) != OS_DYNLOAD_OK) {
        DEBUG_FUNCTION_LINE_ERR("OSDynLoad_FindExport for MEMFreeToDefaultHeap");
        OSFatal("MemoryMappingModule: OSDynLoad_FindExport for MEMFreeToDefaultHeap");
    }

    gMEMAllocFromDefaultHeapExForThreads = (void *(*) (uint32_t, int) ) * allocPtr;
    gMEMFreeToDefaultHeapForThreads      = (void (*)(void *)) * freePtr;

    OSDynLoad_Release(coreinitModule);
}

WUMS_INITIALIZE(args) {
    static uint8_t ucSetupRequired = 1;
    if (!ucSetupRequired) {
        return;
    }

    UpdateFunctionPointer();

    ucSetupRequired = 0;
    MemoryMapping_setupMemoryMapping();
    MemoryMapping_CreateHeaps();

    /* We can not use FunctionPatcher_InitLibrary here because OSDynLoadAcquire is not patched yet.
    if (FunctionPatcher_InitLibrary() != FUNCTION_PATCHER_RESULT_SUCCESS) {
        OSFatal("homebrew_memorymapping: FunctionPatcher_InitLibrary failed");
    }*/

    for (uint32_t i = 0; i < function_replacements_size; i++) {
        bool wasPatched = false;
        // We don't need to save the handles because we never restore them anyway.
        if (FPAddFunctionPatch(&function_replacements[i], nullptr, &wasPatched) != FUNCTION_PATCHER_RESULT_SUCCESS || !wasPatched) {
            OSFatal("homebrew_memorymapping: Failed to patch function");
        }
    }
}

WUMS_APPLICATION_STARTS() {
    OSReport("Running MemoryMappingModule " VERSION VERSION_EXTRA "\n");

    // Now we can update the pointer with the "real" functions
    gMEMAllocFromDefaultHeapExForThreads = MEMAllocFromDefaultHeapEx;
    gMEMFreeToDefaultHeapForThreads      = MEMFreeToDefaultHeap;

#ifdef DEBUG
    initLogging();
#endif
}

#ifdef DEBUG
WUMS_APPLICATION_REQUESTS_EXIT() {
    deinitLogging();
}
#endif

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

uint32_t MEMAllocFromMappedMemory __attribute__((__section__(".data")))         = (uint32_t) MemoryMappingAlloc;
uint32_t MEMAllocFromMappedMemoryEx __attribute__((__section__(".data")))       = (uint32_t) MemoryMappingAllocEx;
uint32_t MEMAllocFromMappedMemoryForGX2Ex __attribute__((__section__(".data"))) = (uint32_t) MemoryMappingAllocForGX2Ex;
uint32_t MEMFreeToMappedMemory __attribute__((__section__(".data")))            = (uint32_t) MemoryMappingFree;

WUMS_EXPORT_FUNCTION(MemoryMappingEffectiveToPhysical);
WUMS_EXPORT_FUNCTION(MemoryMappingPhysicalToEffective);

WUMS_EXPORT_DATA(MEMAllocFromMappedMemory);
WUMS_EXPORT_DATA(MEMAllocFromMappedMemoryEx);
WUMS_EXPORT_DATA(MEMAllocFromMappedMemoryForGX2Ex);
WUMS_EXPORT_DATA(MEMFreeToMappedMemory);
