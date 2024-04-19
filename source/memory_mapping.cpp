#include "memory_mapping.h"
#include <coreinit/cache.h>
#include <coreinit/memexpheap.h>
#include <coreinit/memorymap.h>
#include <coreinit/thread.h>

#include "CThread.h"
#include "logger.h"
#include <coreinit/mutex.h>
#include <cstring>
#include <vector>

// #define DEBUG_FUNCTION_LINE(x,...)

OSMutex allocMutex;

void runOnAllCores(CThread::Callback callback, void *callbackArg, int32_t iAttr = 0, int32_t iPriority = 16, int32_t iStackSize = 0x8000) {
    int32_t aff[] = {CThread::eAttributeAffCore2, CThread::eAttributeAffCore1, CThread::eAttributeAffCore0};

    for (int i : aff) {
        CThread thread(iAttr | i, iPriority, iStackSize, callback, callbackArg);
        thread.resumeThread();
    }
}

void KernelWriteU32(uint32_t addr, uint32_t value) {
    ICInvalidateRange(&value, 4);
    DCFlushRange(&value, 4);

    auto dst = (uint32_t) OSEffectiveToPhysical(addr);
    auto src = (uint32_t) OSEffectiveToPhysical((uint32_t) &value);

    KernelCopyData(dst, src, 4);

    DCFlushRange((void *) addr, 4);
    ICInvalidateRange((void *) addr, 4);
}

void KernelWrite(uint32_t addr, const void *data, uint32_t length) {
    // This is a hacky workaround, but currently it only works this way. ("data" is always on the stack, so maybe a problem with mapping values from the JIT area?)
    // further testing required.
    for (uint32_t i = 0; i < length; i += 4) {
        KernelWriteU32(addr + i, *(uint32_t *) (((uint32_t) data) + i));
    }
}


/*
static void SCSetupIBAT4DBAT5() {
    asm volatile("sync; eieio; isync");

    // Give our and the kernel full execution rights.
    // 00800000-01000000 => 30800000-31000000 (read/write, user/supervisor)
    unsigned int ibat4u = 0x008000FF;
    unsigned int ibat4l = 0x30800012;
    asm volatile("mtspr 560, %0" :: "r"(ibat4u));
    asm volatile("mtspr 561, %0" :: "r"(ibat4l));

    // Give our and the kernel full data access rights.
    // 00800000-01000000 => 30800000-31000000 (read/write, user/supervisor)
    unsigned int dbat5u = ibat4u;
    unsigned int dbat5l = ibat4l;
    asm volatile("mtspr 570, %0" :: "r"(dbat5u));
    asm volatile("mtspr 571, %0" :: "r"(dbat5l));

    asm volatile("eieio; isync");
}
*/
const uint32_t sSCSetupIBAT4DBAT5Buffer[] = {0x7c0004ac,
                                             0x7c0006ac,
                                             0x4c00012c,
                                             0x3d400080,
                                             0x614a00ff,
                                             0x7d508ba6,
                                             0x3d203080,
                                             0x61290012,
                                             0x7d318ba6,
                                             0x7d5a8ba6,
                                             0x7d3b8ba6,
                                             0x7c0006ac,
                                             0x4c00012c,
                                             0x4e800020};

#define TARGET_ADDRESS_EXECUTABLE_MEM 0x017FF000
#define SCSetupIBAT4DBAT5_ADDRESS     TARGET_ADDRESS_EXECUTABLE_MEM

const uint32_t sSC0x51Buffer[] = {
        0x7c7082a6,                                        // mfspr r3, 528
        0x60630003,                                        // ori r3, r3, 0x03
        0x7c7083a6,                                        // mtspr 528, r3
        0x7c7282a6,                                        // mfspr r3, 530
        0x60630003,                                        // ori r3, r3, 0x03
        0x7c7283a6,                                        // mtspr 530, r3
        0x7c0006ac,                                        // eieio
        0x4c00012c,                                        // isync
        0x3c600000 | (SCSetupIBAT4DBAT5_ADDRESS >> 16),    // lis r3, SCSetupIBAT4DBAT5@h
        0x60630000 | (SCSetupIBAT4DBAT5_ADDRESS & 0xFFFF), // ori r3, r3, SCSetupIBAT4DBAT5@l
        0x7c6903a6,                                        // mtctr   r3
        0x4e800420,                                        // bctr
};

#define SC0x51Buffer_ADDRESS (SCSetupIBAT4DBAT5_ADDRESS + sizeof(sSCSetupIBAT4DBAT5Buffer))
#define SC0x51Call_ADDRESS   (SC0x51Buffer_ADDRESS + sizeof(sSC0x51Buffer))

const uint32_t sSC0x51CallBuffer[] = {
        0x38005100, //li %r0, 0x5100
        0x44000002, // sc
        0x4e800020  //blr
};

void SetupIBAT4DBAT5OnAllCores() {
    unsigned char backupBuffer[0x74];
    KernelWrite((uint32_t) backupBuffer, (void *) TARGET_ADDRESS_EXECUTABLE_MEM, sizeof(backupBuffer));

    static_assert(sizeof(backupBuffer) >= (sizeof(sSC0x51Buffer) + sizeof(sSCSetupIBAT4DBAT5Buffer) + sizeof(sSC0x51CallBuffer)), "Not enough memory in backup buffer");
    static_assert(SCSetupIBAT4DBAT5_ADDRESS >= TARGET_ADDRESS_EXECUTABLE_MEM && SCSetupIBAT4DBAT5_ADDRESS < (TARGET_ADDRESS_EXECUTABLE_MEM + sizeof(backupBuffer)), "buffer in wrong memory region");
    static_assert(SC0x51Buffer_ADDRESS >= TARGET_ADDRESS_EXECUTABLE_MEM && SC0x51Buffer_ADDRESS < (TARGET_ADDRESS_EXECUTABLE_MEM + sizeof(backupBuffer)), "buffer in wrong memory region");
    static_assert(SC0x51Call_ADDRESS >= TARGET_ADDRESS_EXECUTABLE_MEM && SC0x51Call_ADDRESS < (TARGET_ADDRESS_EXECUTABLE_MEM + sizeof(backupBuffer)), "buffer in wrong memory region");
    static_assert(SCSetupIBAT4DBAT5_ADDRESS != SC0x51Buffer_ADDRESS && SCSetupIBAT4DBAT5_ADDRESS != SC0x51Call_ADDRESS && SC0x51Buffer_ADDRESS != SC0x51Call_ADDRESS, "buffer are not different");

    // We need copy the functions to a memory region which is executable on all 3 cores
    KernelWrite(SCSetupIBAT4DBAT5_ADDRESS, sSCSetupIBAT4DBAT5Buffer, sizeof(sSCSetupIBAT4DBAT5Buffer)); // Set IBAT5 and DBAT5 to map the memory region
    KernelWrite(SC0x51Buffer_ADDRESS, sSC0x51Buffer, sizeof(sSC0x51Buffer));                            // Implementation of 0x51 syscall
    KernelWrite(SC0x51Call_ADDRESS, sSC0x51CallBuffer, sizeof(sSC0x51CallBuffer));                      // Call of 0x51 syscall

    /* set our setup syscall to an unused position */
    KernelPatchSyscall(0x51, SCSetupIBAT4DBAT5_ADDRESS);

    // We want to run this on all 3 cores.
    {
        int32_t aff[] = {CThread::eAttributeAffCore2, CThread::eAttributeAffCore1, CThread::eAttributeAffCore0};

        int iStackSize = 0x200;

        //! allocate the thread and stack on the default Cafe OS heap
        auto *pThread      = (OSThread *) gMEMAllocFromDefaultHeapExForThreads(sizeof(OSThread), 0x10);
        auto *pThreadStack = (uint8_t *) gMEMAllocFromDefaultHeapExForThreads(iStackSize, 0x20);
        //! create the thread
        if (pThread && pThreadStack) {
            for (int i : aff) {
                *pThread = {};
                memset(pThreadStack, 0, iStackSize);
                OSCreateThread(pThread, reinterpret_cast<OSThreadEntryPointFn>(SC0x51Call_ADDRESS), 0, nullptr, (void *) (pThreadStack + iStackSize), iStackSize, 16, (OSThreadAttributes) i);
                OSResumeThread(pThread);

                while (OSIsThreadSuspended(pThread)) {
                    OSResumeThread(pThread);
                }
                OSJoinThread(pThread, nullptr);
            }
        }

        //! free the thread stack buffer
        if (pThreadStack) {
            memset(pThreadStack, 0, iStackSize);
            gMEMFreeToDefaultHeapForThreads(pThreadStack);
        }
        if (pThread) {
            memset(pThread, 0, sizeof(OSThread));
            gMEMFreeToDefaultHeapForThreads(pThread);
        }
    }

    /* repair data */
    KernelWrite(TARGET_ADDRESS_EXECUTABLE_MEM, backupBuffer, sizeof(backupBuffer));
    DCFlushRange((void *) TARGET_ADDRESS_EXECUTABLE_MEM, sizeof(backupBuffer));
}

void writeKernelNOPs(CThread *thread, void *arg) {
    DEBUG_FUNCTION_LINE_VERBOSE("Writing kernel NOPs on core %d", OSGetThreadAffinity(OSGetCurrentThread()) / 2);

    // Patch out any writes to SR
    int sr = MEMORY_START_BASE >> 28;
    KernelNOPAtPhysicalAddress(0xfff1d734 + 0x4 * sr);
    if (sr < 7) {
        KernelNOPAtPhysicalAddress(0xfff1d604 + 0x4 * sr);
    } else {
        KernelNOPAtPhysicalAddress(0xfff1d648 + 0x4 * (sr - 7));
    }
    KernelNOPAtPhysicalAddress(0xffe00618 + 0x4 * sr);

    // nop out branches to app panic 0x17
    KernelNOPAtPhysicalAddress(0xfff01db0);
    KernelNOPAtPhysicalAddress(0xfff01e90);
    KernelNOPAtPhysicalAddress(0xfff01ea0);
    KernelNOPAtPhysicalAddress(0xfff01ea4);

    // nop out branches to app panic 0x12
    KernelNOPAtPhysicalAddress(0xfff01a00);
    KernelNOPAtPhysicalAddress(0xfff01b68);
    KernelNOPAtPhysicalAddress(0xfff01b70);
    KernelNOPAtPhysicalAddress(0xfff01b7c);
    KernelNOPAtPhysicalAddress(0xfff01b80);

    // nop out branches to app panic 0x16
    KernelNOPAtPhysicalAddress(0xfff0db24);
    KernelNOPAtPhysicalAddress(0xfff0dbb4);
    KernelNOPAtPhysicalAddress(0xfff0dbbc);
    KernelNOPAtPhysicalAddress(0xfff0dbc8);
    KernelNOPAtPhysicalAddress(0xfff0dbcc);

    // nop out branches to app panic 0x14
    KernelNOPAtPhysicalAddress(0xfff01cfc);
    KernelNOPAtPhysicalAddress(0xfff01d4c);
    KernelNOPAtPhysicalAddress(0xfff01d54);
    KernelNOPAtPhysicalAddress(0xfff01d60);
    KernelNOPAtPhysicalAddress(0xfff01d64);
}

void writeSegmentRegister(CThread *thread, void *arg) {
    auto *table = (sr_table_t *) arg;
    DEBUG_FUNCTION_LINE_VERBOSE("Writing segment register to core %d", OSGetThreadAffinity(OSGetCurrentThread()) / 2);

    DCFlushRange(table, sizeof(sr_table_t));
    KernelWriteSRs(table);
}

void readAndPrintSegmentRegister(CThread *thread, void *arg) {
    DEBUG_FUNCTION_LINE_VERBOSE("Reading segment register and page table from core %d", OSGetThreadAffinity(OSGetCurrentThread()) / 2);
    sr_table_t srTable;
    memset(&srTable, 0, sizeof(srTable));

    KernelReadSRs(&srTable);
    DCFlushRange(&srTable, sizeof(srTable));

    for (int32_t i = 0; i < 16; i++) {
        DEBUG_FUNCTION_LINE_VERBOSE("[%d] SR[%d]=%08X", OSGetThreadAffinity(OSGetCurrentThread()) / 2, i, srTable.value[i]);
    }

    uint32_t pageTable[0x8000];

    memset(pageTable, 0, sizeof(pageTable));
    DEBUG_FUNCTION_LINE_VERBOSE("Reading pageTable now.");
    KernelReadPTE((uint32_t) pageTable, sizeof(pageTable));
    DCFlushRange(pageTable, sizeof(pageTable));
    DEBUG_FUNCTION_LINE_VERBOSE("Reading pageTable done");

    MemoryMapping_printPageTableTranslation(srTable, pageTable);

    DEBUG_FUNCTION_LINE_VERBOSE("-----------------------------");
}

bool MemoryMapping_isMemoryMapped() {
    sr_table_t srTable;
    memset(&srTable, 0, sizeof(srTable));

    KernelReadSRs(&srTable);
    if ((srTable.value[MEMORY_START_BASE >> 28] & 0x00FFFFFF) == SEGMENT_UNIQUE_ID) {
        return true;
    }
    return false;
}

void MemoryMapping_searchEmptyMemoryRegions() {
#ifdef DEBUG
    DEBUG_FUNCTION_LINE("Searching for empty memory.");

    for (int32_t i = 0;; i++) {
        if (mem_mapping[i].physical_addresses == nullptr) {
            break;
        }
        uint32_t ea_start_address = mem_mapping[i].effective_start_address;

        const memory_values_t *mem_vals = mem_mapping[i].physical_addresses;

        uint32_t ea_size = 0;
        for (uint32_t j = 0;; j++) {
            uint32_t pa_start_address = mem_vals[j].start_address;
            uint32_t pa_end_address   = mem_vals[j].end_address;
            if (pa_end_address == 0 && pa_start_address == 0) {
                break;
            }
            ea_size += pa_end_address - pa_start_address;
        }

        auto *flush_start   = (uint32_t *) ea_start_address;
        uint32_t flush_size = ea_size;

        DEBUG_FUNCTION_LINE("Flushing %08X (%d kB) at %08X.", flush_size, flush_size / 1024, flush_start);
        DCFlushRange(flush_start, flush_size);

        DEBUG_FUNCTION_LINE("Searching in memory region %d. 0x%08X - 0x%08X. Size 0x%08X (%d KBytes).", i + 1, ea_start_address, ea_start_address + ea_size, ea_size, ea_size / 1024);
        bool success          = true;
        auto *memory_ptr      = (uint32_t *) ea_start_address;
        bool inFailRange      = false;
        uint32_t startFailing = 0;
        uint32_t startGood    = ea_start_address;
        for (uint32_t j = 0; j < ea_size / 4; j++) {
            if (memory_ptr[j] != 0) {
                success = false;
                if (!inFailRange) {
                    if ((((uint32_t) &memory_ptr[j]) - (uint32_t) startGood) / 1024 > 512) {
                        uint32_t start_addr = startGood & 0xFFFE0000;
                        if (start_addr != startGood) {
                            start_addr += 0x20000;
                        }
                        uint32_t end_addr = ((uint32_t) &memory_ptr[j]) - MEMORY_START_BASE;
                        end_addr          = (end_addr & 0xFFFE0000);
                        DEBUG_FUNCTION_LINE("+ Free between 0x%08X and 0x%08X size: %u kB", start_addr - MEMORY_START_BASE, end_addr,
                                            (((uint32_t) end_addr) - ((uint32_t) startGood - MEMORY_START_BASE)) / 1024);
                    }
                    startFailing = (uint32_t) &memory_ptr[j];
                    inFailRange  = true;
                    startGood    = 0;
                    j            = ((j & 0xFFFF8000) + 0x00008000) - 1;
                }
                //break;
            } else {
                if (inFailRange) {
                    //DEBUG_FUNCTION_LINE("- Error between 0x%08X and 0x%08X size: %u kB",startFailing,&memory_ptr[j],(((uint32_t)&memory_ptr[j])-(uint32_t)startFailing)/1024);
                    startFailing = 0;
                    startGood    = (uint32_t) &memory_ptr[j];
                    inFailRange  = false;
                }
            }
        }
        if (startGood != 0 && (startGood != ea_start_address + ea_size)) {
            DEBUG_FUNCTION_LINE("+ Good  between 0x%08X and 0x%08X size: %u kB", startGood - MEMORY_START_BASE, ((uint32_t) (ea_start_address + ea_size) - (uint32_t) MEMORY_START_BASE),
                                ((uint32_t) (ea_start_address + ea_size) - (uint32_t) startGood) / 1024);
        } else if (inFailRange) {
            DEBUG_FUNCTION_LINE("- Used between 0x%08X and 0x%08X size: %u kB", startFailing, ea_start_address + ea_size, ((uint32_t) (ea_start_address + ea_size) - (uint32_t) startFailing) / 1024);
        }
        if (success) {
            DEBUG_FUNCTION_LINE("Test %d was successful!", i + 1);
        }
    }
    DEBUG_FUNCTION_LINE("All tests done.");
#endif
}

void MemoryMapping_writeTestValuesToMemory() {
    //don't smash the stack.
    uint32_t chunk_size = 0x1000;
    uint32_t testBuffer[chunk_size];

    for (int32_t i = 0;; i++) {
        if (mem_mapping[i].physical_addresses == nullptr) {
            break;
        }
        uint32_t cur_ea_start_address = mem_mapping[i].effective_start_address;

        DEBUG_FUNCTION_LINE("Preparing memory test for region %d. Region start at effective address %08X.", i + 1, cur_ea_start_address);

        const memory_values_t *mem_vals = mem_mapping[i].physical_addresses;
        uint32_t counter                = 0;
        for (uint32_t j = 0;; j++) {
            uint32_t pa_start_address = mem_vals[j].start_address;
            uint32_t pa_end_address   = mem_vals[j].end_address;
            if (pa_end_address == 0 && pa_start_address == 0) {
                break;
            }
            uint32_t pa_size = pa_end_address - pa_start_address;
            DEBUG_FUNCTION_LINE("Writing region %d of mapping %d. From %08X to %08X Size: %d KBytes...", j + 1, i + 1, pa_start_address, pa_end_address, pa_size / 1024);
            for (uint32_t k = 0; k <= pa_size / 4; k++) {
                if (k > 0 && (k % chunk_size) == 0) {
                    DCFlushRange(&testBuffer, sizeof(testBuffer));
                    DCInvalidateRange(&testBuffer, sizeof(testBuffer));
                    uint32_t destination = pa_start_address + ((k * 4) - sizeof(testBuffer));
                    KernelCopyData(destination, (uint32_t) OSEffectiveToPhysical((uint32_t) testBuffer), sizeof(testBuffer));
                    //DEBUG_FUNCTION_LINE("Copy testBuffer into %08X",destination);
                }
                if (k != pa_size / 4) {
                    testBuffer[k % chunk_size] = counter++;
                }
                //DEBUG_FUNCTION_LINE("testBuffer[%d] = %d",i % chunk_size,i);
            }
            auto *flush_start   = (uint32_t *) cur_ea_start_address;
            uint32_t flush_size = pa_size;

            cur_ea_start_address += pa_size;

            DEBUG_FUNCTION_LINE("Flushing %08X (%d kB) at %08X to map memory.", flush_size, flush_size / 1024, flush_start);
            DCFlushRange(flush_start, flush_size);
        }

        DEBUG_FUNCTION_LINE("Done writing region %d", i + 1);
    }
}

void MemoryMapping_readTestValuesFromMemory() {
#ifdef DEBUG
    DEBUG_FUNCTION_LINE("Testing reading the written values.");

    for (int32_t i = 0;; i++) {
        if (mem_mapping[i].physical_addresses == nullptr) {
            break;
        }
        uint32_t ea_start_address = mem_mapping[i].effective_start_address;

        const memory_values_t *mem_vals = mem_mapping[i].physical_addresses;
        //uint32_t counter = 0;
        uint32_t ea_size = 0;
        for (uint32_t j = 0;; j++) {
            uint32_t pa_start_address = mem_vals[j].start_address;
            uint32_t pa_end_address   = mem_vals[j].end_address;
            if (pa_end_address == 0 && pa_start_address == 0) {
                break;
            }
            ea_size += pa_end_address - pa_start_address;
        }

        auto *flush_start   = (uint32_t *) ea_start_address;
        uint32_t flush_size = ea_size;

        DEBUG_FUNCTION_LINE("Flushing %08X (%d kB) at %08X to map memory.", flush_size, flush_size / 1024, flush_start);
        DCFlushRange(flush_start, flush_size);

        DEBUG_FUNCTION_LINE("Testing memory region %d. 0x%08X - 0x%08X. Size 0x%08X (%d KBytes).", i + 1, ea_start_address, ea_start_address + ea_size, ea_size, ea_size / 1024);
        bool success          = true;
        auto *memory_ptr      = (uint32_t *) ea_start_address;
        bool inFailRange      = false;
        uint32_t startFailing = 0;
        uint32_t startGood    = ea_start_address;
        for (uint32_t j = 0; j < ea_size / 4; j++) {
            if (memory_ptr[j] != j) {
                success = false;
                if (!inFailRange) {
                    DEBUG_FUNCTION_LINE("+ Good  between 0x%08X and 0x%08X size: %u kB", startGood, &memory_ptr[j], (((uint32_t) &memory_ptr[j]) - (uint32_t) startGood) / 1024);
                    startFailing = (uint32_t) &memory_ptr[j];
                    inFailRange  = true;
                    startGood    = 0;
                    j            = ((j & 0xFFFF8000) + 0x00008000) - 1;
                }
                //break;
            } else {
                if (inFailRange) {
                    DEBUG_FUNCTION_LINE("- Error between 0x%08X and 0x%08X size: %u kB", startFailing, &memory_ptr[j], (((uint32_t) &memory_ptr[j]) - (uint32_t) startFailing) / 1024);
                    startFailing = 0;
                    startGood    = (uint32_t) &memory_ptr[j];
                    inFailRange  = false;
                }
            }
        }
        if (startGood != 0 && (startGood != ea_start_address + ea_size)) {
            DEBUG_FUNCTION_LINE("+ Good  between 0x%08X and 0x%08X size: %u kB", startGood, ea_start_address + ea_size, ((uint32_t) (ea_start_address + ea_size) - (uint32_t) startGood) / 1024);
        } else if (inFailRange) {
            DEBUG_FUNCTION_LINE("- Error between 0x%08X and 0x%08X size: %u kB", startFailing, ea_start_address + ea_size, ((uint32_t) (ea_start_address + ea_size) - (uint32_t) startFailing) / 1024);
        }
        if (success) {
            DEBUG_FUNCTION_LINE("Test %d was successful!", i + 1);
        }
    }
    DEBUG_FUNCTION_LINE("All tests done.");
#endif
}

void MemoryMapping_memoryMappingForRegions(const memory_mapping_t *memory_mapping, sr_table_t SRTable, uint32_t *translation_table) {
    for (int32_t i = 0; /* waiting for a break */; i++) {
        //DEBUG_FUNCTION_LINE("In loop %d",i);
        if (memory_mapping[i].physical_addresses == nullptr) {
            //DEBUG_FUNCTION_LINE("break %d",i);
            break;
        }
        uint32_t cur_ea_start_address = memory_mapping[i].effective_start_address;

        DEBUG_FUNCTION_LINE_VERBOSE("Mapping area %d. effective address %08X...", i + 1, cur_ea_start_address);
        const memory_values_t *mem_vals = memory_mapping[i].physical_addresses;

        for (uint32_t j = 0;; j++) {
            //DEBUG_FUNCTION_LINE("In inner loop %d",j);
            uint32_t pa_start_address = mem_vals[j].start_address;
            uint32_t pa_end_address   = mem_vals[j].end_address;
            if (pa_end_address == 0 && pa_start_address == 0) {
                //DEBUG_FUNCTION_LINE("inner break %d",j);
                // Break if entry was empty.
                break;
            }
            uint32_t pa_size = pa_end_address - pa_start_address;
            DEBUG_FUNCTION_LINE_VERBOSE("Adding page table entry %d for mapping area %d. %08X-%08X => %08X-%08X...", j + 1, i + 1, cur_ea_start_address,
                                        memory_mapping[i].effective_start_address + pa_size, pa_start_address, pa_end_address);
            if (!MemoryMapping_mapMemory(pa_start_address, pa_end_address, cur_ea_start_address, SRTable, translation_table)) {
                //log_print("error =(");
                DEBUG_FUNCTION_LINE("Failed to map memory.");
                //OSFatal("Failed to map memory.");
                return;
                break;
            }
            cur_ea_start_address += pa_size;
            //log_print("done");
        }
    }
}

void MemoryMapping_setupMemoryMapping() {
    /*
     * We need to make sure that with have full access to the 0x0080000-0x01000000 region on all 3 cores.
     */
    SetupIBAT4DBAT5OnAllCores();

    // Override all writes to SR8 with nops.
    // Override some memory region checks inside the kernel
    runOnAllCores(writeKernelNOPs, nullptr);

    //runOnAllCores(readAndPrintSegmentRegister,nullptr,0,16,0x80000);

    sr_table_t srTableCpy;

    uint32_t sizePageTable = sizeof(uint32_t) * 0x8000;
    auto *pageTableCpy     = (uint32_t *) gMEMAllocFromDefaultHeapExForThreads(sizePageTable, 0x10);
    if (!pageTableCpy) {
        OSFatal("MemoryMappingModule: Failed to alloc memory for page table");
    }

    KernelReadSRs(&srTableCpy);
    KernelReadPTE((uint32_t) pageTableCpy, sizePageTable);

    DCFlushRange(&srTableCpy, sizeof(srTableCpy));
    DCFlushRange(pageTableCpy, sizePageTable);

    for (int32_t i = 0; i < 16; i++) {
        DEBUG_FUNCTION_LINE_VERBOSE("SR[%d]=%08X", i, srTableCpy.value[i]);
    }

    //printPageTableTranslation(srTableCpy,pageTableCpy);

    // According to
    // http://wiiubrew.org/wiki/Cafe_OS#Virtual_Memory_Map 0x80000000
    // is currently unmapped.
    // This is nice because it leads to SR[8] which also seems to be unused (was set to 0x30FFFFFF)
    // The content of the segment was chosen randomly.
    uint32_t segment_index   = MEMORY_START_BASE >> 28;
    uint32_t segment_content = 0x00000000 | SEGMENT_UNIQUE_ID;

    DEBUG_FUNCTION_LINE_VERBOSE("Setting SR[%d] to %08X", segment_index, segment_content);
    srTableCpy.value[segment_index] = segment_content;
    DCFlushRange(&srTableCpy, sizeof(srTableCpy));

    DEBUG_FUNCTION_LINE_VERBOSE("Writing segment registers...", segment_index, segment_content);
    // Writing the segment registers to ALL cores.
    //
    //writeSegmentRegister(nullptr, &srTableCpy);

    runOnAllCores(writeSegmentRegister, &srTableCpy);

    MemoryMapping_memoryMappingForRegions(mem_mapping, srTableCpy, pageTableCpy);

    //printPageTableTranslation(srTableCpy,pageTableCpy);

    DEBUG_FUNCTION_LINE_VERBOSE("Writing PageTable... ");
    DCFlushRange(pageTableCpy, sizePageTable);
    KernelWritePTE((uint32_t) pageTableCpy, sizePageTable);
    DCFlushRange(pageTableCpy, sizePageTable);
    DEBUG_FUNCTION_LINE_VERBOSE("done");

    //printPageTableTranslation(srTableCpy,pageTableCpy);

    //runOnAllCores(readAndPrintSegmentRegister,nullptr,0,16,0x80000);

    //searchEmptyMemoryRegions();

    //writeTestValuesToMemory();
    //readTestValuesFromMemory();

    //runOnAllCores(writeSegmentRegister,&srTableCpy);
    OSInitMutex(&allocMutex);

    memset(pageTableCpy, 0, sizePageTable);
    gMEMFreeToDefaultHeapForThreads(pageTableCpy);
}

void *MemoryMapping_allocEx(uint32_t size, int32_t align, bool videoOnly) {
    OSLockMutex(&allocMutex);
    void *res = nullptr;
    for (int32_t i = 0; /* waiting for a break */; i++) {
        if (mem_mapping[i].physical_addresses == nullptr) {
            break;
        }
        uint32_t effectiveAddress = mem_mapping[i].effective_start_address;
        auto heapHandle           = (MEMHeapHandle) effectiveAddress;

        // Skip non-video memory
        if (videoOnly && ((effectiveAddress < MEMORY_START_VIDEO) || (effectiveAddress > MEMORY_END_VIDEO))) {
            continue;
        }

        uint32_t allocSize;
        if (align > 0) {
            allocSize = (size + align - 1) & ~(align - 1);
        } else {
            uint32_t alignAbs = -align;
            allocSize         = (size + alignAbs - 1) & ~(alignAbs - 1);
        }

        res = MEMAllocFromExpHeapEx(heapHandle, allocSize, align);
        if (res != nullptr) {
            break;
        }
    }
    OSMemoryBarrier();
    OSUnlockMutex(&allocMutex);
    return res;
}

extern "C" bool MEMCheckExpHeap(MEMHeapHandle heap, bool logProblems);

void MemoryMapping_checkHeaps() {
    OSLockMutex(&allocMutex);
    for (int32_t i = 0; /* waiting for a break */; i++) {
        if (mem_mapping[i].physical_addresses == nullptr) {
            break;
        }
        auto heapHandle = (MEMHeapHandle) mem_mapping[i].effective_start_address;
        if (!MEMCheckExpHeap(heapHandle, true)) {
            DEBUG_FUNCTION_LINE_ERR("MemoryMapping heap %08X (index %d) is corrupted.", heapHandle, i);
#ifdef DEBUG
            OSFatal("MemoryMappingModule: Heap is corrupted");
#endif
        }
    }
    OSUnlockMutex(&allocMutex);
}

void *MemoryMapping_alloc(uint32_t size, int32_t align) {
    return MemoryMapping_allocEx(size, align, false);
}

void *MemoryMapping_allocVideoMemory(uint32_t size, int32_t align) {
    return MemoryMapping_allocEx(size, align, true);
}

// clang-format off
#define FindHeapContainingBlock ((MEMHeapHandle (*) (MEMMemoryList *, void *) )(0x101C400 + 0x2f2d8))
// clang-format on

MEMHeapHandle MemoryMapping_MEMFindContainHeap(void *block) {
    for (int32_t i = 0; /* waiting for a break */; i++) {
        if (mem_mapping[i].physical_addresses == nullptr) {
            break;
        }
        uint32_t effectiveAddress = mem_mapping[i].effective_start_address;
        auto heapHandle           = (MEMHeapHandle) effectiveAddress;
        auto *heap                = (MEMExpHeap *) heapHandle;
        if (block >= heap->header.dataStart &&
            block < heap->header.dataEnd) {
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
            auto child = FindHeapContainingBlock(&heap->header.list, block);
            return child ? child : heapHandle;
        }
    }

    return nullptr;
}

void MemoryMapping_free(void *ptr) {
    if (ptr == nullptr) {
        return;
    }
    OSLockMutex(&allocMutex);
    auto ptr_val = (uint32_t) ptr;
    for (int32_t i = 0; /* waiting for a break */; i++) {
        if (mem_mapping[i].physical_addresses == nullptr) {
            break;
        }
        if (ptr_val > mem_mapping[i].effective_start_address && ptr_val < mem_mapping[i].effective_end_address) {
            auto heapHandle = (MEMHeapHandle) mem_mapping[i].effective_start_address;
            MEMFreeToExpHeap(heapHandle, ptr);
            break;
        }
    }
    OSMemoryBarrier();
    OSUnlockMutex(&allocMutex);
}

uint32_t MemoryMapping_MEMGetAllocatableSize() {
    return MemoryMapping_MEMGetAllocatableSizeEx(4);
}

uint32_t MemoryMapping_MEMGetAllocatableSizeEx(uint32_t align) {
    OSLockMutex(&allocMutex);
    uint32_t res = 0;
    for (int32_t i = 0; /* waiting for a break */; i++) {
        if (mem_mapping[i].physical_addresses == nullptr) {
            break;
        }
        uint32_t curRes = MEMGetAllocatableSizeForExpHeapEx((MEMHeapHandle) mem_mapping[i].effective_start_address, align);
        DEBUG_FUNCTION_LINE_VERBOSE("heap at %08X MEMGetAllocatableSizeForExpHeapEx: %d KiB", mem_mapping[i].effective_start_address, curRes / 1024);
        if (curRes > res) {
            res = curRes;
        }
    }
    OSUnlockMutex(&allocMutex);
    return res;
}

uint32_t MemoryMapping_GetFreeSpace() {
    OSLockMutex(&allocMutex);
    uint32_t res = 0;
    for (int32_t i = 0; /* waiting for a break */; i++) {
        if (mem_mapping[i].physical_addresses == nullptr) {
            break;
        }
        uint32_t curRes = MEMGetTotalFreeSizeForExpHeap((MEMHeapHandle) mem_mapping[i].effective_start_address);
        DEBUG_FUNCTION_LINE_VERBOSE("heap at %08X MEMGetTotalFreeSizeForExpHeap: %d KiB", mem_mapping[i].effective_start_address, curRes / 1024);
        res += curRes;
    }
    OSUnlockMutex(&allocMutex);
    return res;
}

void MemoryMapping_CreateHeaps() {
    OSLockMutex(&allocMutex);
    for (int32_t i = 0; /* waiting for a break */; i++) {
        if (mem_mapping[i].physical_addresses == nullptr) {
            break;
        }
        void *address = (void *) (mem_mapping[i].effective_start_address);
        uint32_t size = mem_mapping[i].effective_end_address - mem_mapping[i].effective_start_address;

        memset(reinterpret_cast<void *>(mem_mapping[i].effective_start_address), 0, size);
#ifdef DEBUG
        auto heap =
#endif
                MEMCreateExpHeapEx(address, size, MEM_HEAP_FLAG_USE_LOCK);
#ifdef DEBUG
        DEBUG_FUNCTION_LINE("Created heap @%08X, size %d KiB", heap, size / 1024);
#endif
    }
    OSUnlockMutex(&allocMutex);
}

void MemoryMapping_DestroyHeaps() {
    OSLockMutex(&allocMutex);
    for (int32_t i = 0; /* waiting for a break */; i++) {
        if (mem_mapping[i].physical_addresses == nullptr) {
            break;
        }
        void *address = (void *) (mem_mapping[i].effective_start_address);
        uint32_t size = mem_mapping[i].effective_end_address - mem_mapping[i].effective_start_address;

        MEMDestroyExpHeap((MEMHeapHandle) address);
        memset(address, 0, size);
        DEBUG_FUNCTION_LINE_VERBOSE("Destroyed heap @%08X", address);
    }
    OSUnlockMutex(&allocMutex);
}

uint32_t MemoryMapping_getAreaSizeFromPageTable(uint32_t start, uint32_t maxSize) {
    sr_table_t srTable;
    uint32_t pageTable[0x8000];

    KernelReadSRs(&srTable);
    KernelReadPTE((uint32_t) pageTable, sizeof(pageTable));

    uint32_t sr_start = start >> 28;
    uint32_t sr_end   = (start + maxSize) >> 28;

    if (sr_end < sr_start) {
        return 0;
    }

    uint32_t cur_address = start;
    uint32_t end_address = start + maxSize;

    uint32_t memSize = 0;

    for (uint32_t segment = sr_start; segment <= sr_end; segment++) {
        uint32_t sr = srTable.value[segment];
        if (sr >> 31) {
            DEBUG_FUNCTION_LINE("Direct access not supported");
        } else {
            uint32_t vsid = sr & 0xFFFFFF;

            uint32_t pageSize     = 1 << PAGE_INDEX_SHIFT;
            uint32_t cur_end_addr = 0;
            if (segment == sr_end) {
                cur_end_addr = end_address;
            } else {
                cur_end_addr = (segment + 1) * 0x10000000;
            }
            if (segment != sr_start) {
                cur_address = (segment) *0x10000000;
            }
            bool success = true;
            for (uint32_t addr = cur_address; addr < cur_end_addr; addr += pageSize) {
                uint32_t PTEH = 0;
                uint32_t PTEL = 0;
                if (MemoryMapping_getPageEntryForAddress(srTable.sdr1, addr, vsid, pageTable, &PTEH, &PTEL, false)) {
                    memSize += pageSize;
                } else {
                    success = false;
                    break;
                }
            }
            if (!success) {
                break;
            }
        }
    }
    return memSize;
}

bool MemoryMapping_getPageEntryForAddress(uint32_t SDR1, uint32_t addr, uint32_t vsid, uint32_t *translation_table, uint32_t *oPTEH, uint32_t *oPTEL, bool checkSecondHash) {
    uint32_t pageMask    = SDR1 & 0x1FF;
    uint32_t pageIndex   = (addr >> PAGE_INDEX_SHIFT) & PAGE_INDEX_MASK;
    uint32_t primaryHash = (vsid & 0x7FFFF) ^ pageIndex;

    if (MemoryMapping_getPageEntryForAddressEx(SDR1, addr, vsid, primaryHash, translation_table, oPTEH, oPTEL, 0)) {
        return true;
    }

    if (checkSecondHash) {
        if (MemoryMapping_getPageEntryForAddressEx(pageMask, addr, vsid, ~primaryHash, translation_table, oPTEH, oPTEL, 1)) {
            return true;
        }
    }

    return false;
}

bool MemoryMapping_getPageEntryForAddressEx(uint32_t pageMask, uint32_t addr, uint32_t vsid, uint32_t primaryHash, uint32_t *translation_table, uint32_t *oPTEH, uint32_t *oPTEL, uint32_t H) {
    uint32_t maskedHash = primaryHash & ((pageMask << 10) | 0x3FF);
    uint32_t api        = (addr >> 22) & 0x3F;

    uint32_t pteAddrOffset = (maskedHash << 6);

    for (int32_t j = 0; j < 8; j++, pteAddrOffset += 8) {
        uint32_t PTEH = 0;
        uint32_t PTEL = 0;

        uint32_t pteh_index = pteAddrOffset / 4;
        uint32_t ptel_index = pteh_index + 1;

        PTEH = translation_table[pteh_index];
        PTEL = translation_table[ptel_index];

        //Check validity
        if (!(PTEH >> 31)) {
            //printf("PTE is not valid ");
            continue;
        }
        //DEBUG_FUNCTION_LINE("in");
        // the H bit indicated if the PTE was found using the second hash.
        if (((PTEH >> 6) & 1) != H) {
            //DEBUG_FUNCTION_LINE("Secondary hash is used",((PTEH >> 6) & 1));
            continue;
        }

        // Check if the VSID matches, otherwise this is a PTE for another SR
        // This is the place where collision could happen.
        // Hopefully no collision happen and only the PTEs of the SR will match.
        if (((PTEH >> 7) & 0xFFFFFF) != vsid) {
            //DEBUG_FUNCTION_LINE("VSID mismatch");
            continue;
        }

        // Check the API (Abbreviated Page Index)
        if ((PTEH & 0x3F) != api) {
            //DEBUG_FUNCTION_LINE("API mismatch");
            continue;
        }
        *oPTEH = PTEH;
        *oPTEL = PTEL;

        return true;
    }
    return false;
}

void MemoryMapping_printPageTableTranslation(sr_table_t srTable, uint32_t *translation_table) {
    uint32_t SDR1 = srTable.sdr1;

    pageInformation current;
    memset(&current, 0, sizeof(current));

    std::vector<pageInformation> pageInfos;

    for (uint32_t segment = 0; segment < 16; segment++) {
        uint32_t sr = srTable.value[segment];
        if (sr >> 31) {
            DEBUG_FUNCTION_LINE_VERBOSE("Direct access not supported");
        } else {
            uint32_t ks   = (sr >> 30) & 1;
            uint32_t kp   = (sr >> 29) & 1;
            uint32_t nx   = (sr >> 28) & 1;
            uint32_t vsid = sr & 0xFFFFFF;

            DEBUG_FUNCTION_LINE_VERBOSE("ks   %08X kp   %08X nx   %08X vsid %08X", ks, kp, nx, vsid);
            uint32_t pageSize = 1 << PAGE_INDEX_SHIFT;
            for (uint32_t addr = segment * 0x10000000; addr < (segment + 1) * 0x10000000; addr += pageSize) {
                uint32_t PTEH = 0;
                uint32_t PTEL = 0;
                if (MemoryMapping_getPageEntryForAddress(SDR1, addr, vsid, translation_table, &PTEH, &PTEL, false)) {
                    uint32_t pp   = PTEL & 3;
                    uint32_t phys = PTEL & 0xFFFFF000;

                    //DEBUG_FUNCTION_LINE("current.phys == phys - current.size ( %08X %08X)",current.phys, phys - current.size);

                    if (current.ks == ks &&
                        current.kp == kp &&
                        current.nx == nx &&
                        current.pp == pp &&
                        current.phys == phys - current.size) {
                        current.size += pageSize;
                        //DEBUG_FUNCTION_LINE("New size of %08X is %08X",current.addr,current.size);
                    } else {
                        if (current.addr != 0 && current.size != 0) {
                            /*DEBUG_FUNCTION_LINE("Saving old block from %08X",current.addr);
                            DEBUG_FUNCTION_LINE("ks %08X new %08X",current.ks,ks);
                            DEBUG_FUNCTION_LINE("kp %08X new %08X",current.kp,kp);
                            DEBUG_FUNCTION_LINE("nx %08X new %08X",current.nx,nx);
                            DEBUG_FUNCTION_LINE("pp %08X new %08X",current.pp,pp);*/
                            pageInfos.push_back(current);
                            memset(&current, 0, sizeof(current));
                        }
                        //DEBUG_FUNCTION_LINE("Found new block at %08X",addr);
                        current.addr = addr;
                        current.size = pageSize;
                        current.kp   = kp;
                        current.ks   = ks;
                        current.nx   = nx;
                        current.pp   = pp;
                        current.phys = phys;
                    }
                } else {
                    if (current.addr != 0 && current.size != 0) {
                        pageInfos.push_back(current);
                        memset(&current, 0, sizeof(current));
                    }
                }
            }
        }
    }

#ifdef VERBOSE_DEBUG
    const char *access1[] = {"read/write", "read/write", "read/write", "read only"};
    const char *access2[] = {"no access", "read only", "read/write", "read only"};

    for (auto cur : pageInfos) {
        DEBUG_FUNCTION_LINE_VERBOSE("%08X %08X -> %08X %08X. user access %s. supervisor access %s. %s", cur.addr, cur.addr + cur.size, cur.phys, cur.phys + cur.size,
                                    cur.kp ? access2[cur.pp] : access1[cur.pp],
                                    cur.ks ? access2[cur.pp] : access1[cur.pp], cur.nx ? "not executable" : "executable");
    }
#endif
}


bool MemoryMapping_mapMemory(uint32_t pa_start_address, uint32_t pa_end_address, uint32_t ea_start_address, sr_table_t SRTable, uint32_t *translation_table) {
    // Based on code from dimok. Thanks!

    //uint32_t byteOffsetMask = (1 << PAGE_INDEX_SHIFT) - 1;
    //uint32_t apiShift = 22 - PAGE_INDEX_SHIFT;

    // Information on page 5.
    // https://www.nxp.com/docs/en/application-note/AN2794.pdf
    uint32_t HTABORG  = SRTable.sdr1 >> 16;
    uint32_t HTABMASK = SRTable.sdr1 & 0x1FF;

    // Iterate to all possible pages. Each page is 1<<(PAGE_INDEX_SHIFT) big.

    uint32_t pageSize = 1 << (PAGE_INDEX_SHIFT);
    for (uint32_t i = 0; i < pa_end_address - pa_start_address; i += pageSize) {
        // Calculate the current effective address.
        uint32_t ea_addr = ea_start_address + i;
        // Calculate the segement.
        uint32_t segment = SRTable.value[ea_addr >> 28];

        // Unique ID from the segment which is the input for the hash function.
        // Change it to prevent collisions.
        uint32_t VSID = segment & 0x00FFFFFF;
        uint32_t V    = 1;

        //Indicated if second hash is used.
        uint32_t H = 0;

        // Abbreviated Page Index

        // Real page number
        uint32_t RPN  = (pa_start_address + i) >> 12;
        uint32_t RC   = 3;
        uint32_t WIMG = 0x02;
        uint32_t PP   = 0x02;

        uint32_t page_index = (ea_addr >> PAGE_INDEX_SHIFT) & PAGE_INDEX_MASK;
        uint32_t API        = (ea_addr >> 22) & 0x3F;

        uint32_t PTEH = (V << 31) | (VSID << 7) | (H << 6) | API;
        uint32_t PTEL = (RPN << 12) | (RC << 7) | (WIMG << 3) | PP;

        //unsigned long long virtual_address = ((unsigned long long)VSID << 28UL) | (page_index << PAGE_INDEX_SHIFT) | (ea_addr & 0xFFF);

        uint32_t primary_hash = (VSID & 0x7FFFF);

        uint32_t hashvalue1 = primary_hash ^ page_index;

        // hashvalue 2 is the complement of the first hash.
        uint32_t hashvalue2 = ~hashvalue1;

        //uint32_t pageMask = SRTable.sdr1 & 0x1FF;

        // calculate the address of the PTE groups.
        // PTEs are saved in a group of 8 PTEs
        // When PTEGaddr1 is full (all 8 PTEs set), PTEGaddr2 is used.
        // Then H in PTEH needs to be set to 1.
        uint32_t PTEGaddr1 = (HTABORG << 16) | (((hashvalue1 >> 10) & HTABMASK) << 16) | ((hashvalue1 & 0x3FF) << 6);
        uint32_t PTEGaddr2 = (HTABORG << 16) | (((hashvalue2 >> 10) & HTABMASK) << 16) | ((hashvalue2 & 0x3FF) << 6);

        //offset of the group inside the PTE Table.
        uint32_t PTEGoffset = PTEGaddr1 - (HTABORG << 16);

        bool setSuccessfully = false;
        PTEGoffset += 7 * 8;
        // Lets iterate through the PTE group where out PTE should be saved.
        for (int32_t j = 7; j > 0; PTEGoffset -= 8) {
            int32_t index = (PTEGoffset / 4);

            uint32_t pteh = translation_table[index];
            // Check if it's already taken. The first bit indicates if the PTE-slot inside
            // this group is already taken.
            if (pteh == 0) {
                // If we found a free slot, set the PTEH and PTEL value.
                //DEBUG_FUNCTION_LINE("Used slot %d. PTEGaddr1 %08X addr %08X",j+1,PTEGaddr1 - (HTABORG << 16),PTEGoffset);
                translation_table[index]     = PTEH;
                translation_table[index + 1] = PTEL;
                setSuccessfully              = true;
                break;
            } else {
                //printf("PTEGoffset %08X was taken",PTEGoffset);
            }
            j--;
        }
        // Check if we already found a slot.
        if (!setSuccessfully) {
            //DEBUG_FUNCTION_LINE("-------------- Using second slot -----------------------");
            // We still have a chance to find a slot in the PTEGaddr2 using the complement of the hash.
            // We need to set the H flag in PTEH and use PTEGaddr2.
            // (Not well tested)
            H          = 1;
            PTEH       = (V << 31) | (VSID << 7) | (H << 6) | API;
            PTEGoffset = PTEGaddr2 - (HTABORG << 16);
            PTEGoffset += 7 * 8;
            // Same as before.
            for (int32_t j = 7; j > 0; PTEGoffset -= 8) {
                int32_t index = (PTEGoffset / 4);
                uint32_t pteh = translation_table[index];
                //Check if it's already taken.
                if (pteh == 0) {
                    translation_table[index]     = PTEH;
                    translation_table[index + 1] = PTEL;
                    setSuccessfully              = true;
                    break;
                } else {
                    //printf("PTEGoffset %08X was taken",PTEGoffset);
                }
                j--;
            }

            if (!setSuccessfully) {
                // Fail if we couldn't find a free slot.
                DEBUG_FUNCTION_LINE("-------------- No more free PTE -----------------------");
                return false;
            }
        }
    }
    return true;
}

uint32_t MemoryMapping_PhysicalToEffective(uint32_t phyiscalAddress) {
    if (phyiscalAddress >= 0x30800000 && phyiscalAddress < 0x31000000) {
        return phyiscalAddress - (0x30800000 - 0x00800000);
    }

    uint32_t result                     = 0;
    const memory_values_t *curMemValues = nullptr;
    //iterate through all own mapped memory regions
    for (int32_t i = 0; true; i++) {
        if (mem_mapping[i].physical_addresses == nullptr) {
            break;
        }

        curMemValues           = mem_mapping[i].physical_addresses;
        uint32_t curOffsetInEA = 0;
        // iterate through all memory values of this region
        for (int32_t j = 0; true; j++) {
            if (curMemValues[j].end_address == 0) {
                break;
            }
            if (phyiscalAddress >= curMemValues[j].start_address && phyiscalAddress < curMemValues[j].end_address) {
                // calculate the EA
                result = (phyiscalAddress - curMemValues[j].start_address) + (mem_mapping[i].effective_start_address + curOffsetInEA);
                return result;
            }
            curOffsetInEA += curMemValues[j].end_address - curMemValues[j].start_address;
        }
    }

    return result;
}

uint32_t MemoryMapping_EffectiveToPhysical(uint32_t effectiveAddress) {
    if (effectiveAddress >= 0x00800000 && effectiveAddress < 0x01000000) {
        return effectiveAddress + (0x30800000 - 0x00800000);
    }

    uint32_t result = 0;
    // CAUTION: The data may be fragmented between multiple areas in PA.
    const memory_values_t *curMemValues = nullptr;
    uint32_t curOffset                  = 0;

    for (int32_t i = 0; true; i++) {
        if (mem_mapping[i].physical_addresses == nullptr) {
            break;
        }
        if (effectiveAddress >= mem_mapping[i].effective_start_address && effectiveAddress < mem_mapping[i].effective_end_address) {
            curMemValues = mem_mapping[i].physical_addresses;
            curOffset    = mem_mapping[i].effective_start_address;
            break;
        }
    }

    if (curMemValues == nullptr) {
        return result;
    }

    for (int32_t i = 0; true; i++) {
        if (curMemValues[i].end_address == 0) {
            break;
        }
        uint32_t curChunkSize = curMemValues[i].end_address - curMemValues[i].start_address;
        if (effectiveAddress < (curOffset + curChunkSize)) {
            result = (effectiveAddress - curOffset) + curMemValues[i].start_address;
            break;
        }
        curOffset += curChunkSize;
    }
    return result;
}
