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

#include <pthread.h>

#include <inttypes.h>
#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>

#include "private/bionic_string_utils.h"
#include "private/ErrnoRestorer.h"
#include "private/libc_logging.h"
#include "pthread_internal.h"

int pthread_attr_init(pthread_attr_t* attr) {
  attr->flags = 0;
  attr->stack_base = NULL;
  attr->stack_size = PTHREAD_STACK_SIZE_DEFAULT;
  attr->guard_size = PAGE_SIZE;
  attr->sched_policy = SCHED_NORMAL;
  attr->sched_priority = 0;
  return 0;
}

int pthread_attr_destroy(pthread_attr_t* attr) {
  memset(attr, 0x42, sizeof(pthread_attr_t));
  return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t* attr, int state) {
  if (state == PTHREAD_CREATE_DETACHED) {
    attr->flags |= PTHREAD_ATTR_FLAG_DETACHED;
  } else if (state == PTHREAD_CREATE_JOINABLE) {
    attr->flags &= ~PTHREAD_ATTR_FLAG_DETACHED;
  } else {
    return EINVAL;
  }
  return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t* attr, int* state) {
  *state = (attr->flags & PTHREAD_ATTR_FLAG_DETACHED) ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE;
  return 0;
}

int pthread_attr_setschedpolicy(pthread_attr_t* attr, int policy) {
  attr->sched_policy = policy;
  return 0;
}

int pthread_attr_getschedpolicy(const pthread_attr_t* attr, int* policy) {
  *policy = attr->sched_policy;
  return 0;
}

int pthread_attr_setschedparam(pthread_attr_t* attr, const sched_param* param) {
  attr->sched_priority = param->sched_priority;
  return 0;
}

int pthread_attr_getschedparam(const pthread_attr_t* attr, sched_param* param) {
  param->sched_priority = attr->sched_priority;
  return 0;
}

int pthread_attr_setstacksize(pthread_attr_t* attr, size_t stack_size) {
  if (stack_size < PTHREAD_STACK_MIN) {
    return EINVAL;
  }
  attr->stack_size = stack_size;
  return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t* attr, size_t* stack_size) {
  void* unused;
  return pthread_attr_getstack(attr, &unused, stack_size);
}

int pthread_attr_setstack(pthread_attr_t* attr, void* stack_base, size_t stack_size) {
  if ((stack_size & (PAGE_SIZE - 1) || stack_size < PTHREAD_STACK_MIN)) {
    return EINVAL;
  }
  if (reinterpret_cast<uintptr_t>(stack_base) & (PAGE_SIZE - 1)) {
    return EINVAL;
  }
  attr->stack_base = stack_base;
  attr->stack_size = stack_size;
  return 0;
}

static int __pthread_attr_getstack_main_thread(void** stack_base, size_t* stack_size) {
  ErrnoRestorer errno_restorer;

  rlimit stack_limit;
  if (getrlimit(RLIMIT_STACK, &stack_limit) == -1) {
    return errno;
  }

  // If the current RLIMIT_STACK is RLIM_INFINITY, only admit to an 8MiB stack for sanity's sake.
  if (stack_limit.rlim_cur == RLIM_INFINITY) {
    stack_limit.rlim_cur = 8 * 1024 * 1024;
  }

  const char* statpath = "/proc/self/stat";
  FILE* fpstat = fopen(statpath, "re");
  if (fpstat == NULL) {
    return errno;
  }

  uintptr_t startstack  = 0;
  // Due to the startstack is the 28th element in /proc/[pid]/stat, before get its value,
  // this snippet code help to discard the first 27 elements of the conversion specification.
  // About the conversion specification，please refer to the man proc "/proc/[pid]/stat" section.
  for (int i = 0; i < 27; ++i) {
    fscanf(fpstat, "%*s ");
  }
  if (fscanf(fpstat, "%" SCNuPTR, &startstack) != 1) {
    __libc_fatal("startstack not found in \"%s\"!", statpath);
  }
  fclose(fpstat);

  const char* mapspath = "/proc/self/maps";
  FILE* fpmaps = fopen(mapspath, "re");
  if (fpmaps == NULL) {
    return errno;
  }
  char line[BUFSIZ];
  while (fgets(line, sizeof(line), fpmaps) != NULL) {
    uintptr_t lo, hi;
    if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR, &lo, &hi) == 2) {
      if (lo <= in_stack && in_stack <= hi) {
        *stack_size = stack_limit.rlim_cur;
        *stack_base = reinterpret_cast<void*>(hi - *stack_size);
        fclose(fpmaps);
        return 0;
      }
    }
  }
  __libc_fatal("Stack line not found in \"%s\"!", mapspath);
}

int pthread_attr_getstack(const pthread_attr_t* attr, void** stack_base, size_t* stack_size) {
  *stack_base = attr->stack_base;
  *stack_size = attr->stack_size;
  return 0;
}

int pthread_attr_setguardsize(pthread_attr_t* attr, size_t guard_size) {
  attr->guard_size = guard_size;
  return 0;
}

int pthread_attr_getguardsize(const pthread_attr_t* attr, size_t* guard_size) {
  *guard_size = attr->guard_size;
  return 0;
}

int pthread_getattr_np(pthread_t t, pthread_attr_t* attr) {
  pthread_internal_t* thread = reinterpret_cast<pthread_internal_t*>(t);
  *attr = thread->attr;
  // We prefer reading join_state here to setting thread->attr.flags in pthread_detach.
  // Because data race exists in the latter case.
  if (atomic_load(&thread->join_state) == THREAD_DETACHED) {
    attr->flags |= PTHREAD_ATTR_FLAG_DETACHED;
  }
  // The main thread's stack information is not stored in thread->attr, and we need to
  // collect that at runtime.
  if (thread->tid == getpid()) {
    return __pthread_attr_getstack_main_thread(&attr->stack_base, &attr->stack_size);
  }
  return 0;
}

int pthread_attr_setscope(pthread_attr_t*, int scope) {
  if (scope == PTHREAD_SCOPE_SYSTEM) {
    return 0;
  }
  if (scope == PTHREAD_SCOPE_PROCESS) {
    return ENOTSUP;
  }
  return EINVAL;
}

int pthread_attr_getscope(const pthread_attr_t*, int* scope) {
  *scope = PTHREAD_SCOPE_SYSTEM;
  return 0;
}
