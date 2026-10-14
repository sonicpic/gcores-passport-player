#pragma once
#include <cstddef>
#define MALLOC_CAP_8BIT 1
size_t heap_caps_get_largest_free_block(unsigned caps);
