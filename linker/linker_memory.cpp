/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "linker_allocator.h"
#include "linker.h"

#include <algorithm>
#include <vector>

#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "private/bionic_prctl.h"

static LinkerMemoryAllocator g_linker_allocator;

void* malloc(size_t byte_count) {
  return g_linker_allocator.alloc(byte_count);
}

void* calloc(size_t item_count, size_t item_size) {
  return g_linker_allocator.alloc(item_count*item_size);
}

void free(void* ptr) {
  g_linker_allocator.free(ptr);
}

