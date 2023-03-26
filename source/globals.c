#include "globals.h"

void *(*gMEMAllocFromDefaultHeapExForThreads)(uint32_t size, int align);
void (*gMEMFreeToDefaultHeapForThreads)(void *ptr);