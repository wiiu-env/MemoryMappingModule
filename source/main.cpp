#include <coreinit/debug.h>
#include <cstddef>
#include <malloc.h>
#include <wums.h>
#include <whb/log.h>
#include <whb/log_udp.h>
#include "memory_mapping.h"
#include "logger.h"

WUMS_MODULE_EXPORT_NAME("homebrew_memorymapping");

WUMS_INITIALIZE(){
    WHBLogUdpInit();
    DEBUG_FUNCTION_LINE("Setting up memory mapping!");
    static uint8_t ucSetupRequired = 1;
    if (!ucSetupRequired){
        return;
    }
    ucSetupRequired = 0;

    MemoryMapping::setupMemoryMapping();
}

int main(int argc, char **argv) {
    return 0;
}

void MemoryMappingFree(void* ptr){
    MemoryMapping::free(ptr);
}
void* MemoryMappingAlloc(uint32_t size, uint32_t align){
    return MemoryMapping::alloc(size, align);
}

uint32_t MemoryMappingEffectiveToPhysical(uint32_t address){
    return MemoryMapping::EffectiveToPhysical(address);
}
uint32_t MemoryMappingPhysicalToEffective(uint32_t address){
    return MemoryMapping::PhysicalToEffective(address);
}

WUMS_EXPORT_FUNCTION(MemoryMappingFree);
WUMS_EXPORT_FUNCTION(MemoryMappingAlloc);
WUMS_EXPORT_FUNCTION(MemoryMappingEffectiveToPhysical);
WUMS_EXPORT_FUNCTION(MemoryMappingPhysicalToEffective);