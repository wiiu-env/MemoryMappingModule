#include "function_replacements.h"

#include "memory_mapping.h"


DECL_FUNCTION(uint32_t, KiEffectiveToPhysical, uint32_t addressSpace, uint32_t virtualAddress) {
    uint32_t result = real_KiEffectiveToPhysical(addressSpace, virtualAddress);
    if (result == 0) {
        return MemoryMapping_EffectiveToPhysical(virtualAddress);
    }
    return result;
}

DECL_FUNCTION(int32_t, sCheckDataRange, uint32_t address, uint32_t maxDataSize) {
    if ((address & 0xF0000000) == 0x80000000) {
        return 1;
    }

    return real_sCheckDataRange(address, maxDataSize);
}

DECL_FUNCTION(uint32_t, KiPhysicalToEffectiveCached, uint32_t addressSpace, uint32_t virtualAddress) {
    uint32_t result = real_KiPhysicalToEffectiveCached(addressSpace, virtualAddress);
    if (result == 0) {
        return MemoryMapping_PhysicalToEffective(virtualAddress);
    }
    return result;
}

DECL_FUNCTION(uint32_t, KiPhysicalToEffectiveUncached, uint32_t addressSpace, uint32_t virtualAddress) {
    uint32_t result = real_KiPhysicalToEffectiveUncached(addressSpace, virtualAddress);
    if (result == 0) {
        return MemoryMapping_PhysicalToEffective(virtualAddress);

    }
    return result;
}

DECL_FUNCTION(uint32_t, IPCKDriver_ValidatePhysicalAddress, uint32_t u1, uint32_t physStart, uint32_t physEnd) {
    uint32_t result = MemoryMapping_PhysicalToEffective(physStart) > 0;
    if (result) {
        return result;
    }

    return real_IPCKDriver_ValidatePhysicalAddress(u1, physStart, physEnd);
}

DECL_FUNCTION(uint32_t, KiIsEffectiveRangeValid, uint32_t addressSpace, uint32_t virtualAddress, uint32_t size) {
    uint32_t result = real_KiIsEffectiveRangeValid(addressSpace, virtualAddress, size);
    if (result == 0) {
        result = MemoryMapping_EffectiveToPhysical(virtualAddress) > 0;
    }
    return result;
}

function_replacement_data_t function_replacements[] __attribute__((section(".data"))) = {
        REPLACE_FUNCTION_VIA_ADDRESS(sCheckDataRange,                       0x3200cf60, 0x0100cf60),
        REPLACE_FUNCTION_VIA_ADDRESS(KiEffectiveToPhysical,                 0xffee0aac, 0xffee0aac),
        REPLACE_FUNCTION_VIA_ADDRESS(KiPhysicalToEffectiveCached,           0xffee0a3c, 0xffee0a3c),
        REPLACE_FUNCTION_VIA_ADDRESS(KiPhysicalToEffectiveUncached,         0xffee0a80, 0xffee0a80),
        REPLACE_FUNCTION_VIA_ADDRESS(KiIsEffectiveRangeValid,               0xffee0d6c, 0xffee0d6c),
        REPLACE_FUNCTION_VIA_ADDRESS(IPCKDriver_ValidatePhysicalAddress,    0xfff0cb5c, 0xfff0cb5c),
};

uint32_t function_replacements_size __attribute__((section(".data"))) = sizeof(function_replacements) / sizeof(function_replacement_data_t);