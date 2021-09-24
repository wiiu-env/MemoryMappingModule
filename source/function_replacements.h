#pragma once

#include <function_patcher/function_patching.h>
#include <cstdint>

extern function_replacement_data_t function_replacements[] __attribute__((section(".data")));

extern uint32_t function_replacements_size __attribute__((section(".data")));