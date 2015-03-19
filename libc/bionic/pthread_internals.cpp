/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include "pthread_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "private/bionic_futex.h"
#include "private/bionic_tls.h"
#include "private/libc_logging.h"
#include "private/ScopedPthreadReadWriteLocker.h"

static pthread_internal_t* g_thread_list = NULL;
static pthread_rwlock_t g_thread_list_lock = PTHREAD_RWLOCK_INITIALIZER;

void __add_pthread_internal(pthread_internal_t* thread) {
  ScopedPthreadWriteLocker locker(&g_thread_list_lock);

  // We insert at the head.
  thread->next = g_thread_list;
  thread->prev = NULL;
  if (thread->next != NULL) {
    thread->next->prev = thread;
  }
  g_thread_list = thread;
}

void __remove_pthread_internal(pthread_internal_t* thread) {
  ScopedPthreadWriteLocker locker(&g_thread_list_lock);

  if (thread->next != NULL) {
    thread->next->prev = thread->prev;
  }
  if (thread->prev != NULL) {
    thread->prev->next = thread->next;
  } else {
    g_thread_list = thread->next;
  }
}

void __free_pthread_internal(pthread_internal_t* thread) {
  __remove_pthread_internal(thread);
  if (thread->mmap_size != 0) {
    // Free mapped space, including thread stack and pthread_internal_t.
    munmap(thread->attr.stack_base, thread->mmap_size);
  }
}

bool __is_valid_pthread_internal(pthread_internal_t* thread) {
  ScopedPthreadReadLocker locker(&g_thread_list_lock);

  for (pthread_internal_t* t = g_thread_list; t != NULL; t = t->next) {
    if (t == thread) {
      return true;
    }
  }
  return false;
}
