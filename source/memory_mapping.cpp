#include "memory_mapping.h"
#include <coreinit/memorymap.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/cache.h>
#include <coreinit/memexpheap.h>
#include <coreinit/thread.h>

#include <vector>
#include "memory.h"
#include "logger.h"
#include "CThread.h"
#include <stdio.h>
#include <string.h>

// #define DEBUG_FUNCTION_LINE(x,...)

void runOnAllCores(CThread::Callback callback, void *callbackArg, int32_t iAttr = 0, int32_t iPriority = 16, int32_t iStackSize = 0x8000) {
    int32_t aff[] = {CThread::eAttributeAffCore2, CThread::eAttributeAffCore1, CThread::eAttributeAffCore0};

    for (uint32_t i = 0; i < (sizeof(aff) / sizeof(aff[0])); i++) {
        CThread *thread = CThread::create(callback, callbackArg, iAttr | aff[i], iPriority, iStackSize);
        thread->resumeThread();
        delete thread;
    }
}

void writeKernelNOPs(CThread *thread, void *arg) {
    uint16_t core = OSGetThreadAffinity(OSGetCurrentThread());
    DEBUG_FUNCTION_LINE("Writing kernel NOPs on core %d", core/2);

    KernelNOPAtPhysicalAddress(0xFFF1D754);
    KernelNOPAtPhysicalAddress(0xFFF1D64C);
    KernelNOPAtPhysicalAddress(0xFFE00638);

    KernelNOPAtPhysicalAddress(0xfff01db0);
    KernelNOPAtPhysicalAddress(0xfff01db4);
    KernelNOPAtPhysicalAddress(0xfff01a00);
    KernelNOPAtPhysicalAddress(0xfff01a04);
    KernelNOPAtPhysicalAddress(0xfff01e90);
    KernelNOPAtPhysicalAddress(0xfff01ea0);
    KernelNOPAtPhysicalAddress(0xfff01ea4);

    KernelNOPAtPhysicalAddress(0xfff0db24);
    KernelNOPAtPhysicalAddress(0xfff0dbb4);
    KernelNOPAtPhysicalAddress(0xfff0dbbc);
    KernelNOPAtPhysicalAddress(0xfff0dbc8);
    KernelNOPAtPhysicalAddress(0xfff0dbcc);
}

void writeSegmentRegister(CThread *thread, void *arg) {
    sr_table_t *table = (sr_table_t *) arg;
    uint16_t core = OSGetThreadAffinity(OSGetCurrentThread());
    DEBUG_FUNCTION_LINE("Writing segment register to core %d", core/2);

    DCFlushRange(table, sizeof(sr_table_t));
    KernelWriteSRs(table);
}

void readAndPrintSegmentRegister(CThread *thread, void *arg) {
    uint16_t core = OSGetThreadAffinity(OSGetCurrentThread());
    DEBUG_FUNCTION_LINE("Reading segment register and page table from core %d", core/2);
    sr_table_t srTable;
    memset(&srTable, 0, sizeof(srTable));

    KernelReadSRs(&srTable);
    DCFlushRange(&srTable, sizeof(srTable));

    for (int32_t i = 0; i < 16; i++) {
        DEBUG_FUNCTION_LINE("[%d] SR[%d]=%08X", core, i, srTable.value[i]);
    }

    uint32_t pageTable[0x8000];

    memset(pageTable, 0, sizeof(pageTable));
    DEBUG_FUNCTION_LINE("Reading pageTable now.");
    KernelReadPTE((uint32_t) pageTable, sizeof(pageTable));
    DCFlushRange(pageTable, sizeof(pageTable));
    DEBUG_FUNCTION_LINE("Reading pageTable done");

    MemoryMapping_printPageTableTranslation(srTable, pageTable);

    DEBUG_FUNCTION_LINE("-----------------------------");
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
    DEBUG_FUNCTION_LINE("Searching for empty memory.");

    for (int32_t i = 0;; i++) {
        if (mem_mapping[i].physical_addresses == NULL) {
            break;
        }
        uint32_t ea_start_address = mem_mapping[i].effective_start_address;

        const memory_values_t *mem_vals = mem_mapping[i].physical_addresses;

        uint32_t ea_size = 0;
        for (uint32_t j = 0;; j++) {
            uint32_t pa_start_address = mem_vals[j].start_address;
            uint32_t pa_end_address = mem_vals[j].end_address;
            if (pa_end_address == 0 && pa_start_address == 0) {
                break;
            }
            ea_size += pa_end_address - pa_start_address;
        }

        uint32_t *flush_start = (uint32_t *) ea_start_address;
        uint32_t flush_size = ea_size;

        DEBUG_FUNCTION_LINE("Flushing %08X (%d kB) at %08X.", flush_size, flush_size / 1024, flush_start);
        DCFlushRange(flush_start, flush_size);

        DEBUG_FUNCTION_LINE("Searching in memory region %d. 0x%08X - 0x%08X. Size 0x%08X (%d KBytes).", i + 1, ea_start_address, ea_start_address + ea_size, ea_size, ea_size / 1024);
        bool success = true;
        uint32_t *memory_ptr = (uint32_t *) ea_start_address;
        bool inFailRange = false;
        uint32_t startFailing = 0;
        uint32_t startGood = ea_start_address;
        for (uint32_t j = 0; j < ea_size / 4; j++) {
            if (memory_ptr[j] != 0) {
                success = false;
                if (!success && !inFailRange) {
                    if ((((uint32_t) &memory_ptr[j]) - (uint32_t) startGood) / 1024 > 512) {
                        uint32_t start_addr = startGood & 0xFFFE0000;
                        if (start_addr != startGood) {
                            start_addr += 0x20000;
                        }
                        uint32_t end_addr = ((uint32_t) &memory_ptr[j]) - MEMORY_START_BASE;
                        end_addr = (end_addr & 0xFFFE0000);
                        DEBUG_FUNCTION_LINE("+ Free between 0x%08X and 0x%08X size: %u kB", start_addr - MEMORY_START_BASE, end_addr, (((uint32_t) end_addr) - ((uint32_t) startGood - MEMORY_START_BASE)) / 1024);
                    }
                    startFailing = (uint32_t) &memory_ptr[j];
                    inFailRange = true;
                    startGood = 0;
                    j = ((j & 0xFFFF8000) + 0x00008000) - 1;
                }
                //break;
            } else {
                if (inFailRange) {
                    //DEBUG_FUNCTION_LINE("- Error between 0x%08X and 0x%08X size: %u kB",startFailing,&memory_ptr[j],(((uint32_t)&memory_ptr[j])-(uint32_t)startFailing)/1024);
                    startFailing = 0;
                    startGood = (uint32_t) &memory_ptr[j];
                    inFailRange = false;
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
}

void MemoryMapping_writeTestValuesToMemory() {
    //don't smash the stack.
    uint32_t chunk_size = 0x1000;
    uint32_t testBuffer[chunk_size];

    for (int32_t i = 0;; i++) {
        if (mem_mapping[i].physical_addresses == NULL) {
            break;
        }
        uint32_t cur_ea_start_address = mem_mapping[i].effective_start_address;

        DEBUG_FUNCTION_LINE("Preparing memory test for region %d. Region start at effective address %08X.", i + 1, cur_ea_start_address);

        const memory_values_t *mem_vals = mem_mapping[i].physical_addresses;
        uint32_t counter = 0;
        for (uint32_t j = 0;; j++) {
            uint32_t pa_start_address = mem_vals[j].start_address;
            uint32_t pa_end_address = mem_vals[j].end_address;
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
            uint32_t *flush_start = (uint32_t *) cur_ea_start_address;
            uint32_t flush_size = pa_size;

            cur_ea_start_address += pa_size;

            DEBUG_FUNCTION_LINE("Flushing %08X (%d kB) at %08X to map memory.", flush_size, flush_size / 1024, flush_start);
            DCFlushRange(flush_start, flush_size);
        }

        DEBUG_FUNCTION_LINE("Done writing region %d", i + 1);
    }
}

void MemoryMapping_readTestValuesFromMemory() {
    DEBUG_FUNCTION_LINE("Testing reading the written values.");

    for (int32_t i = 0;; i++) {
        if (mem_mapping[i].physical_addresses == NULL) {
            break;
        }
        uint32_t ea_start_address = mem_mapping[i].effective_start_address;

        const memory_values_t *mem_vals = mem_mapping[i].physical_addresses;
        //uint32_t counter = 0;
        uint32_t ea_size = 0;
        for (uint32_t j = 0;; j++) {
            uint32_t pa_start_address = mem_vals[j].start_address;
            uint32_t pa_end_address = mem_vals[j].end_address;
            if (pa_end_address == 0 && pa_start_address == 0) {
                break;
            }
            ea_size += pa_end_address - pa_start_address;
        }

        uint32_t *flush_start = (uint32_t *) ea_start_address;
        uint32_t flush_size = ea_size;

        DEBUG_FUNCTION_LINE("Flushing %08X (%d kB) at %08X to map memory.", flush_size, flush_size / 1024, flush_start);
        DCFlushRange(flush_start, flush_size);

        DEBUG_FUNCTION_LINE("Testing memory region %d. 0x%08X - 0x%08X. Size 0x%08X (%d KBytes).", i + 1, ea_start_address, ea_start_address + ea_size, ea_size, ea_size / 1024);
        bool success = true;
        uint32_t *memory_ptr = (uint32_t *) ea_start_address;
        bool inFailRange = false;
        uint32_t startFailing = 0;
        uint32_t startGood = ea_start_address;
        for (uint32_t j = 0; j < ea_size / 4; j++) {
            if (memory_ptr[j] != j) {
                success = false;
                if (!success && !inFailRange) {
                    DEBUG_FUNCTION_LINE("+ Good  between 0x%08X and 0x%08X size: %u kB", startGood, &memory_ptr[j], (((uint32_t) &memory_ptr[j]) - (uint32_t) startGood) / 1024);
                    startFailing = (uint32_t) &memory_ptr[j];
                    inFailRange = true;
                    startGood = 0;
                    j = ((j & 0xFFFF8000) + 0x00008000) - 1;
                }
                //break;
            } else {
                if (inFailRange) {
                    DEBUG_FUNCTION_LINE("- Error between 0x%08X and 0x%08X size: %u kB", startFailing, &memory_ptr[j], (((uint32_t) &memory_ptr[j]) - (uint32_t) startFailing) / 1024);
                    startFailing = 0;
                    startGood = (uint32_t) &memory_ptr[j];
                    inFailRange = false;
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
}

void MemoryMapping_memoryMappingForRegions(const memory_mapping_t *memory_mapping, sr_table_t SRTable, uint32_t *translation_table) {
    for (int32_t i = 0; /* waiting for a break */; i++) {
        //DEBUG_FUNCTION_LINE("In loop %d",i);
        if (memory_mapping[i].physical_addresses == NULL) {
            //DEBUG_FUNCTION_LINE("break %d",i);
            break;
        }
        uint32_t cur_ea_start_address = memory_mapping[i].effective_start_address;

        DEBUG_FUNCTION_LINE("Mapping area %d. effective address %08X...", i + 1, cur_ea_start_address);
        const memory_values_t *mem_vals = memory_mapping[i].physical_addresses;

        for (uint32_t j = 0;; j++) {
            //DEBUG_FUNCTION_LINE("In inner loop %d",j);
            uint32_t pa_start_address = mem_vals[j].start_address;
            uint32_t pa_end_address = mem_vals[j].end_address;
            if (pa_end_address == 0 && pa_start_address == 0) {
                //DEBUG_FUNCTION_LINE("inner break %d",j);
                // Break if entry was empty.
                break;
            }
            uint32_t pa_size = pa_end_address - pa_start_address;
            DEBUG_FUNCTION_LINE("Adding page table entry %d for mapping area %d. %08X-%08X => %08X-%08X...", j + 1, i + 1, cur_ea_start_address, memory_mapping[i].effective_start_address + pa_size, pa_start_address, pa_end_address);
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
    // Override all writes to SR8 with nops.
    // Override some memory region checks inside the kernel
    runOnAllCores(writeKernelNOPs,NULL);

    //runOnAllCores(readAndPrintSegmentRegister,NULL,0,16,0x80000);

    sr_table_t srTableCpy;
    uint32_t pageTableCpy[0x8000];

    KernelReadSRs(&srTableCpy);
    KernelReadPTE((uint32_t) pageTableCpy, sizeof(pageTableCpy));

    DCFlushRange(&srTableCpy, sizeof(srTableCpy));
    DCFlushRange(pageTableCpy, sizeof(pageTableCpy));

    for (int32_t i = 0; i < 16; i++) {
        DEBUG_FUNCTION_LINE("SR[%d]=%08X", i, srTableCpy.value[i]);
    }

    //printPageTableTranslation(srTableCpy,pageTableCpy);

    // According to
    // http://wiiubrew.org/wiki/Cafe_OS#Virtual_Memory_Map 0x80000000
    // is currently unmapped.
    // This is nice because it leads to SR[8] which also seems to be unused (was set to 0x30FFFFFF)
    // The content of the segment was chosen randomly.
    uint32_t segment_index = MEMORY_START_BASE >> 28;
    uint32_t segment_content = 0x00000000 | SEGMENT_UNIQUE_ID;

    DEBUG_FUNCTION_LINE("Setting SR[%d] to %08X", segment_index, segment_content);
    srTableCpy.value[segment_index] = segment_content;
    DCFlushRange(&srTableCpy, sizeof(srTableCpy));

    DEBUG_FUNCTION_LINE("Writing segment registers...", segment_index, segment_content);
    // Writing the segment registers to ALL cores.
    //
    //writeSegmentRegister(NULL, &srTableCpy);

    runOnAllCores(writeSegmentRegister, &srTableCpy);

    MemoryMapping_memoryMappingForRegions(mem_mapping, srTableCpy, pageTableCpy);

    //printPageTableTranslation(srTableCpy,pageTableCpy);

    DEBUG_FUNCTION_LINE("Writing PageTable... ");
    DCFlushRange(pageTableCpy, sizeof(pageTableCpy));
    KernelWritePTE((uint32_t) pageTableCpy, sizeof(pageTableCpy));
    DCFlushRange(pageTableCpy, sizeof(pageTableCpy));
    DEBUG_FUNCTION_LINE("done");

    //printPageTableTranslation(srTableCpy,pageTableCpy);

    //runOnAllCores(readAndPrintSegmentRegister,NULL,0,16,0x80000);

    //searchEmptyMemoryRegions();

    //writeTestValuesToMemory();
    //readTestValuesFromMemory();

    //runOnAllCores(writeSegmentRegister,&srTableCpy);
}

void *MemoryMapping_alloc(uint32_t size, uint32_t align) {
    void *res = NULL;
    for (int32_t i = 0; /* waiting for a break */; i++) {
        if (mem_mapping[i].physical_addresses == NULL) {
            break;
        }
        MEMHeapHandle heapHandle = (MEMHeapHandle) mem_mapping[i].effective_start_address;
        MEMExpHeap *heap = (MEMExpHeap *) heapHandle;
        OSUninterruptibleSpinLock_Acquire(&heap->header.lock);
        res = MEMAllocFromExpHeapEx(heapHandle, size, align);
        auto cur = heap->usedList.head;
        while (cur != nullptr) {
            DCFlushRange(cur, sizeof(MEMExpHeapBlock));
            cur = cur->next;
        }
        cur = heap->freeList.head;
        while (cur != nullptr) {
            DCFlushRange(cur, sizeof(MEMExpHeapBlock));
            cur = cur->next;
        }
        OSUninterruptibleSpinLock_Release(&heap->header.lock);
        if (res != nullptr) {
            break;
        }
    }
    return res;
}

void *MemoryMapping_allocVideoMemory(uint32_t size, uint32_t align) {
    void *res = NULL;
    for (int32_t i = 0; /* waiting for a break */; i++) {
        if (mem_mapping[i].physical_addresses == NULL) {
            break;
        }
        uint32_t effectiveAddress = mem_mapping[i].effective_start_address;

        // Skip non-video memory
        if(effectiveAddress < MEMORY_START_VIDEO || effectiveAddress > MEMORY_END_VIDEO){
            continue;
        }
        res = MEMAllocFromExpHeapEx((MEMHeapHandle) mem_mapping[i].effective_start_address, size, align);
        if (res != NULL) {
            break;
        }
    }
    return res;
}


void MemoryMapping_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    uint32_t ptr_val = (uint32_t) ptr;
    for (int32_t i = 0; /* waiting for a break */; i++) {
        if (mem_mapping[i].physical_addresses == NULL) {
            break;
        }
        if (ptr_val > mem_mapping[i].effective_start_address && ptr_val < mem_mapping[i].effective_end_address) {
            MEMHeapHandle heapHandle = (MEMHeapHandle) mem_mapping[i].effective_start_address;
            MEMExpHeap *heap = (MEMExpHeap *) heapHandle;
            OSUninterruptibleSpinLock_Acquire(&heap->header.lock);
            MEMFreeToExpHeap((MEMHeapHandle) mem_mapping[i].effective_start_address, ptr);
            auto cur = heap->usedList.head;
            while (cur != nullptr) {
                DCFlushRange(cur, sizeof(MEMExpHeapBlock));
                cur = cur->next;
            }
            cur = heap->freeList.head;
            while (cur != nullptr) {
                DCFlushRange(cur, sizeof(MEMExpHeapBlock));
                cur = cur->next;
            }
            OSUninterruptibleSpinLock_Release(&heap->header.lock);
            break;
        }
    }
}

uint32_t MemoryMapping_MEMGetAllocatableSize() {
    return MemoryMapping_MEMGetAllocatableSizeEx(4);
}

uint32_t MemoryMapping_MEMGetAllocatableSizeEx(uint32_t align) {
    uint32_t res = 0;
    for (int32_t i = 0; /* waiting for a break */; i++) {
        if (mem_mapping[i].physical_addresses == NULL) {
            break;
        }
        uint32_t curRes = MEMGetAllocatableSizeForExpHeapEx((MEMHeapHandle) mem_mapping[i].effective_start_address, align);
        DEBUG_FUNCTION_LINE("heap at %08X MEMGetAllocatableSizeForExpHeapEx: %d KiB", mem_mapping[i].effective_start_address, curRes / 1024);
        if (curRes > res) {
            res = curRes;
        }
    }
    return res;
}

uint32_t MemoryMapping_GetFreeSpace() {
    uint32_t res = 0;
    for (int32_t i = 0; /* waiting for a break */; i++) {
        if (mem_mapping[i].physical_addresses == NULL) {
            break;
        }
        uint32_t curRes = MEMGetTotalFreeSizeForExpHeap((MEMHeapHandle) mem_mapping[i].effective_start_address);
        DEBUG_FUNCTION_LINE("heap at %08X MEMGetTotalFreeSizeForExpHeap: %d KiB", mem_mapping[i].effective_start_address, curRes / 1024);
        res += curRes;
    }
    return res;
}

void MemoryMapping_CreateHeaps() {
    for (int32_t i = 0; /* waiting for a break */; i++) {
        if (mem_mapping[i].physical_addresses == NULL) {
            break;
        }
        void *address = (void *) (mem_mapping[i].effective_start_address);
        uint32_t size = mem_mapping[i].effective_end_address - mem_mapping[i].effective_start_address;

        memset(reinterpret_cast<void *>(mem_mapping[i].effective_start_address), 0, size);
        MEMCreateExpHeapEx(address, size, MEM_HEAP_FLAG_USE_LOCK);
        DEBUG_FUNCTION_LINE("Created heap @%08X, size %d KiB", address, size / 1024);
    }
}

void MemoryMapping_DestroyHeaps() {
    for (int32_t i = 0; /* waiting for a break */; i++) {
        if (mem_mapping[i].physical_addresses == NULL) {
            break;
        }
        void *address = (void *) (mem_mapping[i].effective_start_address);
        uint32_t size = mem_mapping[i].effective_end_address - mem_mapping[i].effective_start_address;

        MEMDestroyExpHeap((MEMHeapHandle) address);
        memset(address, 0, size);
        DEBUG_FUNCTION_LINE("Destroyed heap @%08X", address);
    }
}

uint32_t MemoryMapping_getAreaSizeFromPageTable(uint32_t start, uint32_t maxSize) {
    sr_table_t srTable;
    uint32_t pageTable[0x8000];

    KernelReadSRs(&srTable);
    KernelReadPTE((uint32_t) pageTable, sizeof(pageTable));

    uint32_t sr_start = start >> 28;
    uint32_t sr_end = (start + maxSize) >> 28;

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

            uint32_t pageSize = 1 << PAGE_INDEX_SHIFT;
            uint32_t cur_end_addr = 0;
            if (segment == sr_end) {
                cur_end_addr = end_address;
            } else {
                cur_end_addr = (segment + 1) * 0x10000000;
            }
            if (segment != sr_start) {
                cur_address = (segment) * 0x10000000;
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
    uint32_t pageMask = SDR1 & 0x1FF;
    uint32_t pageIndex = (addr >> PAGE_INDEX_SHIFT) & PAGE_INDEX_MASK;
    uint32_t primaryHash = (vsid & 0x7FFFF) ^pageIndex;

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
    uint32_t api = (addr >> 22) & 0x3F;

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
            DEBUG_FUNCTION_LINE("Direct access not supported");
        } else {
            uint32_t ks = (sr >> 30) & 1;
            uint32_t kp = (sr >> 29) & 1;
            uint32_t nx = (sr >> 28) & 1;
            uint32_t vsid = sr & 0xFFFFFF;

            DEBUG_FUNCTION_LINE("ks   %08X kp   %08X nx   %08X vsid %08X", ks, kp, nx, vsid);
            uint32_t pageSize = 1 << PAGE_INDEX_SHIFT;
            for (uint32_t addr = segment * 0x10000000; addr < (segment + 1) * 0x10000000; addr += pageSize) {
                uint32_t PTEH = 0;
                uint32_t PTEL = 0;
                if (MemoryMapping_getPageEntryForAddress(SDR1, addr, vsid, translation_table, &PTEH, &PTEL, false)) {
                    uint32_t pp = PTEL & 3;
                    uint32_t phys = PTEL & 0xFFFFF000;

                    //DEBUG_FUNCTION_LINE("current.phys == phys - current.size ( %08X %08X)",current.phys, phys - current.size);

                    if (current.ks == ks &&
                        current.kp == kp &&
                        current.nx == nx &&
                        current.pp == pp &&
                        current.phys == phys - current.size
                            ) {
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
                        current.kp = kp;
                        current.ks = ks;
                        current.nx = nx;
                        current.pp = pp;
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

    const char *access1[] = {"read/write", "read/write", "read/write", "read only"};
    const char *access2[] = {"no access", "read only", "read/write", "read only"};

    for (std::vector<pageInformation>::iterator it = pageInfos.begin(); it != pageInfos.end(); ++it) {
        pageInformation cur = *it;
        DEBUG_FUNCTION_LINE("%08X %08X -> %08X %08X. user access %s. supervisor access %s. %s", cur.addr, cur.addr + cur.size, cur.phys, cur.phys + cur.size, cur.kp ? access2[cur.pp] : access1[cur.pp],
                            cur.ks ? access2[cur.pp] : access1[cur.pp], cur.nx ? "not executable" : "executable");
    }
}


bool MemoryMapping_mapMemory(uint32_t pa_start_address, uint32_t pa_end_address, uint32_t ea_start_address, sr_table_t SRTable, uint32_t *translation_table) {
    // Based on code from dimok. Thanks!

    //uint32_t byteOffsetMask = (1 << PAGE_INDEX_SHIFT) - 1;
    //uint32_t apiShift = 22 - PAGE_INDEX_SHIFT;

    // Information on page 5.
    // https://www.nxp.com/docs/en/application-note/AN2794.pdf
    uint32_t HTABORG = SRTable.sdr1 >> 16;
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
        uint32_t V = 1;

        //Indicated if second hash is used.
        uint32_t H = 0;

        // Abbreviated Page Index

        // Real page number
        uint32_t RPN = (pa_start_address + i) >> 12;
        uint32_t RC = 3;
        uint32_t WIMG = 0x02;
        uint32_t PP = 0x02;

        uint32_t page_index = (ea_addr >> PAGE_INDEX_SHIFT) & PAGE_INDEX_MASK;
        uint32_t API = (ea_addr >> 22) & 0x3F;

        uint32_t PTEH = (V << 31) | (VSID << 7) | (H << 6) | API;
        uint32_t PTEL = (RPN << 12) | (RC << 7) | (WIMG << 3) | PP;

        //unsigned long long virtual_address = ((unsigned long long)VSID << 28UL) | (page_index << PAGE_INDEX_SHIFT) | (ea_addr & 0xFFF);

        uint32_t primary_hash = (VSID & 0x7FFFF);

        uint32_t hashvalue1 = primary_hash ^page_index;

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
            if ((pteh == 0)) {
                // If we found a free slot, set the PTEH and PTEL value.
                //DEBUG_FUNCTION_LINE("Used slot %d. PTEGaddr1 %08X addr %08X",j+1,PTEGaddr1 - (HTABORG << 16),PTEGoffset);
                translation_table[index] = PTEH;
                translation_table[index + 1] = PTEL;
                setSuccessfully = true;
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
            H = 1;
            PTEH = (V << 31) | (VSID << 7) | (H << 6) | API;
            PTEGoffset = PTEGaddr2 - (HTABORG << 16);
            PTEGoffset += 7 * 8;
            // Same as before.
            for (int32_t j = 7; j > 0; PTEGoffset -= 8) {
                int32_t index = (PTEGoffset / 4);
                uint32_t pteh = translation_table[index];
                //Check if it's already taken.
                if ((pteh == 0)) {
                    translation_table[index] = PTEH;
                    translation_table[index + 1] = PTEL;
                    setSuccessfully = true;
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

    uint32_t result = 0;
    const memory_values_t *curMemValues = NULL;
    //iterate through all own mapped memory regions
    for (int32_t i = 0; true; i++) {
        if (mem_mapping[i].physical_addresses == NULL) {
            break;
        }

        curMemValues = mem_mapping[i].physical_addresses;
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
    const memory_values_t *curMemValues = NULL;
    uint32_t curOffset = 0;

    for (int32_t i = 0; true; i++) {
        if (mem_mapping[i].physical_addresses == NULL) {
            break;
        }
        if (effectiveAddress >= mem_mapping[i].effective_start_address && effectiveAddress < mem_mapping[i].effective_end_address) {
            curMemValues = mem_mapping[i].physical_addresses;
            curOffset = mem_mapping[i].effective_start_address;
            break;
        }
    }

    if (curMemValues == NULL) {
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


