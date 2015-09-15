/*
 * Copyright (C) 2015 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

#include "private/bionic_safestack.h"

#include "private/bionic_tls.h"
#include "private/bionic_prctl.h"
#include "private/libc_logging.h"


/*
 TODO list.
 1. mmap is not random at all, unsafe stack is _adjacent_ to the regular stack.
    Need some extra randomization.
 2. Consider switching to direct TLS access from the use code instead of
    __safestack_unsafe_stack_ptr_addr for extra performance.
*/

// Default size of the unsafe stack. This value is only used if the stack
// size rlimit is set to infinity.
static const unsigned kDefaultUnsafeStackSize = 0x2800000;

extern "C" void** __safestack_unsafe_stack_ptr_addr() {
  return &(__get_tls()[TLS_SLOT_SAFESTACK]);
}

static void unsafe_stack_alloc(pthread_internal_t* thr, size_t mmap_size,
                               size_t stack_guard_size) {
  const unsigned kNameBufferSize = 32;
  // Create a new private anonymous map.
  int prot = PROT_READ | PROT_WRITE;
  int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
  void* space = mmap(NULL, mmap_size, prot, flags, -1, 0);
  if (space == MAP_FAILED) {
    __libc_format_log(
      ANDROID_LOG_WARN, "libc",
      "pthread_create failed: couldn't allocate %zu-bytes mapped space: %s",
      mmap_size, strerror(errno));
    return;
  }

  char* stack_top = reinterpret_cast<char*>(space) + mmap_size - kNameBufferSize;
  char* stack_bottom = reinterpret_cast<char*>(space) + stack_guard_size;
  char* stack_name = stack_top;

  snprintf(stack_name, kNameBufferSize, "unsafe-stack:%d",
           static_cast<int>(thr->tid));
  prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, stack_bottom,
        mmap_size - stack_guard_size, stack_name);

  // Stack is at the lower end of mapped space, stack guard region is at the
  // lower end of stack.
  // Set the stack guard region to PROT_NONE, so we can detect thread stack
  // overflow.
  if (mprotect(space, stack_guard_size, PROT_NONE) == -1) {
    __libc_format_log(ANDROID_LOG_WARN, "libc",
                      "pthread_create failed: couldn't mprotect PROT_NONE "
                      "%zu-byte stack guard region: %s",
                      stack_guard_size, strerror(errno));
    munmap(space, mmap_size);
    return;
  }
  prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, space, stack_guard_size,
        "unsafe stack guard page");

  thr->unsafe_stack_start = space;
  thr->unsafe_stack_size = mmap_size;

  __get_tls()[TLS_SLOT_SAFESTACK] = stack_top;
}

static void unsafe_stack_free(pthread_internal_t* thr) {
  munmap(thr->unsafe_stack_start, thr->unsafe_stack_size);
  // Just in case...
  thr->unsafe_stack_start = 0;
  thr->unsafe_stack_size = 0;
}

void __init_safestack_main_thread(pthread_internal_t* thr) {
  size_t size = kDefaultUnsafeStackSize;
  size_t guard = 4096;

  struct rlimit limit;
  if (getrlimit(RLIMIT_STACK, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY)
    size = limit.rlim_cur;

  unsafe_stack_alloc(thr, size, guard);
}

void __init_safestack_thread(pthread_internal_t* thr, size_t stack_size, size_t guard) {
  unsafe_stack_alloc(thr, stack_size, guard);
}

void __free_safestack_thread(pthread_internal_t* thr) {
  unsafe_stack_free(thr);
}
