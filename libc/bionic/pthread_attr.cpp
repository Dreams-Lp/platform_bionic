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
#if defined(__USE_GNU)
  attr->cpu_set = NULL;
  attr->cpu_set_size = 0;
#endif
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

  // It shouldn't matter which thread we are because we're just looking for "[stack]", but
  // valgrind seems to mess with the stack enough that the kernel will report "[stack:pid]"
  // instead if you look in /proc/self/maps, so we need to look in /proc/pid/task/pid/maps.
  char path[64];
  snprintf(path, sizeof(path), "/proc/self/task/%d/maps", getpid());
  FILE* fp = fopen(path, "re");
  if (fp == NULL) {
    return errno;
  }
  char line[BUFSIZ];
  while (fgets(line, sizeof(line), fp) != NULL) {
    if (ends_with(line, " [stack]\n")) {
      uintptr_t lo, hi;
      if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR, &lo, &hi) == 2) {
        *stack_size = stack_limit.rlim_cur;
        *stack_base = reinterpret_cast<void*>(hi - *stack_size);
        fclose(fp);
        return 0;
      }
    }
  }
  __libc_fatal("No [stack] line found in \"%s\"!", path);
}

int pthread_attr_getstack(const pthread_attr_t* attr, void** stack_base, size_t* stack_size) {
  if ((attr->flags & PTHREAD_ATTR_FLAG_MAIN_THREAD) != 0) {
    return __pthread_attr_getstack_main_thread(stack_base, stack_size);
  }
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
  *attr = reinterpret_cast<pthread_internal_t*>(t)->attr;
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

#if defined(__USE_GNU)
static size_t __kernel_set_size = 0;
extern "C" int __sched_getaffinity(pid_t, size_t, cpu_set_t*);

static int __check_cpuset_attr(const size_t set_size, const cpu_set_t* set)
{
  size_t i;
  size_t kernel_set_size;

  // Determine the kernel cpuset size if not available yet
  if (__kernel_set_size == 0) {
    int rc;
    size_t probe_size;
    cpu_set_t* kernel_set;

    probe_size = 64;
    kernel_set = reinterpret_cast<cpu_set_t*>(malloc(probe_size));

    // rc holds cpu_set_size that is used internally by the kernel to represent the
    // CPU set bit mask on success.
    while ((rc = __sched_getaffinity(0, probe_size, kernel_set)) == -1) {
      probe_size *= 2;
      kernel_set = reinterpret_cast<cpu_set_t*>(realloc(kernel_set, probe_size));
      if (kernel_set == NULL) {
        return ENOMEM;
      }
    }

    __kernel_set_size = rc;
    free(kernel_set);
  }

  kernel_set_size = __kernel_set_size;

  // Invalid request if there is any bit set beyond kernel set size
  for (i = kernel_set_size; i < set_size; i++) {
    if (reinterpret_cast<const char*>(set)[i] != '\0') {
      return EINVAL;
    }
  }

  return 0;
}

int pthread_attr_getaffinity_np(const pthread_attr_t* attr, size_t cpu_set_size, cpu_set_t* cpu_set) {

  if (attr->cpu_set != NULL) {
    // Return EINVAL if there are bits already set beyond the provided cpu_set_size
    size_t i;
    for (i = cpu_set_size; i < attr->cpu_set_size; i++)
      if (reinterpret_cast<char*>(attr->cpu_set)[i] != 0) {
        return EINVAL;
      }

    if (attr->cpu_set_size < cpu_set_size) {
      memcpy(cpu_set, attr->cpu_set, attr->cpu_set_size);
      // Clear up the extra room
      memset(cpu_set + attr->cpu_set_size, '\0', cpu_set_size - attr->cpu_set_size);
    } else {
      memcpy(cpu_set, attr->cpu_set, attr->cpu_set_size);
    }
  } else {
    // no information available
    memset (cpu_set, -1, cpu_set_size);
  }

  return 0;
}

int pthread_attr_setaffinity_np(pthread_attr_t* attr, size_t set_size, const cpu_set_t* set) {

  if (set == NULL || set_size == 0) {
    attr->cpu_set = NULL;
    attr->cpu_set_size = 0;
  } else {
    // Check input validity
    int ret = __check_cpuset_attr (set_size, set);

    if (ret != 0) {
      return ret;
    }

    if (attr->cpu_set_size != set_size) {
      cpu_set_t* new_set = reinterpret_cast<cpu_set_t*>(realloc(attr->cpu_set, set_size));
      if (new_set == NULL) {
        return ENOMEM;
      }

      attr->cpu_set = new_set;
      attr->cpu_set_size = set_size;
    }

    memcpy (attr->cpu_set, set, set_size);
  }

  return 0;
}
#endif
