#pragma once
#include <stdint.h>

extern void *(*gMEMAllocFromDefaultHeapExForThreads)(uint32_t size, int align);
extern void (*gMEMFreeToDefaultHeapForThreads)(void *ptr);