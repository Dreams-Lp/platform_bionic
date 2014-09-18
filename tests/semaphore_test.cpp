/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include <semaphore.h>

#include <errno.h>
#include <gtest/gtest.h>
#include <limits.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

TEST(semaphore, sem_init) {
  sem_t s;

  // Perfectly fine initial values.
  ASSERT_EQ(0, sem_init(&s, 0, 0));
  ASSERT_EQ(0, sem_init(&s, 0, 1));
  ASSERT_EQ(0, sem_init(&s, 0, 123));

  // Too small an initial value.
  errno = 0;
  ASSERT_EQ(-1, sem_init(&s, 0, -1));
  ASSERT_EQ(EINVAL, errno);

  ASSERT_EQ(SEM_VALUE_MAX, sysconf(_SC_SEM_VALUE_MAX));

  // The largest initial value.
  ASSERT_EQ(0, sem_init(&s, 0, SEM_VALUE_MAX));

  // Too large an initial value.
  errno = 0;
  ASSERT_EQ(-1, sem_init(&s, 0, SEM_VALUE_MAX + 1));
  ASSERT_EQ(EINVAL, errno);

  ASSERT_EQ(0, sem_destroy(&s));
}

TEST(semaphore, sem_trywait) {
  sem_t s;
  ASSERT_EQ(0, sem_init(&s, 0, 3));
  ASSERT_EQ(0, sem_trywait(&s));
  ASSERT_EQ(0, sem_trywait(&s));
  ASSERT_EQ(0, sem_trywait(&s));
  errno = 0;
  ASSERT_EQ(-1, sem_trywait(&s));
  ASSERT_EQ(EAGAIN, errno);
  ASSERT_EQ(0, sem_destroy(&s));
}

static void* SemWaitThreadFn(void* arg) {
  sem_t& s = *reinterpret_cast<sem_t*>(arg);
  return reinterpret_cast<void*>(sem_wait(&s));
}

TEST(semaphore, sem_wait__sem_post) {
  sem_t s;
  ASSERT_EQ(0, sem_init(&s, 0, 0));

  pthread_t t;
  ASSERT_EQ(0, pthread_create(&t, NULL, SemWaitThreadFn, &s));

  ASSERT_EQ(0, sem_post(&s));

  void* result;
  ASSERT_EQ(0, pthread_join(t, &result));

  ASSERT_EQ(0, static_cast<int>(reinterpret_cast<uintptr_t>(result)));
}

static inline void timespec_add_ms(timespec& ts, size_t ms) {
  ts.tv_sec  += ms / 1000;
  ts.tv_nsec += (ms % 1000) * 1000000;
  if (ts.tv_nsec >= 1000000000) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000;
  }
}

TEST(semaphore, sem_timedwait) {
  sem_t s;
  ASSERT_EQ(0, sem_init(&s, 0, 0));

  timespec ts;
  ASSERT_EQ(0, clock_gettime(CLOCK_REALTIME, &ts));
  timespec_add_ms(ts, 100);

  errno = 0;
  ASSERT_EQ(-1, sem_timedwait(&s, &ts));
  ASSERT_EQ(ETIMEDOUT, errno);

  // A negative timeout is an error.
  errno = 0;
  ts.tv_nsec = -1;
  ASSERT_EQ(-1, sem_timedwait(&s, &ts));
  ASSERT_EQ(EINVAL, errno);

  ASSERT_EQ(0, sem_destroy(&s));
}

TEST(semaphore, sem_getvalue) {
  sem_t s;
  ASSERT_EQ(0, sem_init(&s, 0, 0));

  int i;
  ASSERT_EQ(0, sem_getvalue(&s, &i));
  ASSERT_EQ(0, i);

  ASSERT_EQ(0, sem_post(&s));
  ASSERT_EQ(0, sem_getvalue(&s, &i));
  ASSERT_EQ(1, i);

  ASSERT_EQ(0, sem_post(&s));
  ASSERT_EQ(0, sem_getvalue(&s, &i));
  ASSERT_EQ(2, i);

  ASSERT_EQ(0, sem_wait(&s));
  ASSERT_EQ(0, sem_getvalue(&s, &i));
  ASSERT_EQ(1, i);
}

TEST(semaphore, sem_open) {}
TEST(semaphore, sem_close) {}
TEST(semaphore, sem_unlink) {}
