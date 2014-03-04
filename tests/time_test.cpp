/*
 * Copyright (C) 2013 The Android Open Source Project
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

#include <time.h>

#include <errno.h>
#include <features.h>
#include <gtest/gtest.h>
#include <signal.h>

#if defined(__BIONIC__) // mktime_tz is a bionic extension.
#include <libc/private/bionic_time.h>
#endif // __BIONIC__

TEST(time, mktime_tz) {
#if defined(__BIONIC__)
  struct tm epoch;
  memset(&epoch, 0, sizeof(tm));
  epoch.tm_year = 1970 - 1900;
  epoch.tm_mon = 1;
  epoch.tm_mday = 1;

  // Alphabetically first. Coincidentally equivalent to UTC.
  ASSERT_EQ(2678400, mktime_tz(&epoch, "Africa/Abidjan"));

  // Alphabetically last. Coincidentally equivalent to UTC.
  ASSERT_EQ(2678400, mktime_tz(&epoch, "Zulu"));

  // Somewhere in the middle, not UTC.
  ASSERT_EQ(2707200, mktime_tz(&epoch, "America/Los_Angeles"));

  // Missing. Falls back to UTC.
  ASSERT_EQ(2678400, mktime_tz(&epoch, "PST"));
#else // __BIONIC__
  GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif // __BIONIC__
}

TEST(time, gmtime) {
  time_t t = 0;
  tm* broken_down = gmtime(&t);
  ASSERT_TRUE(broken_down != NULL);
  ASSERT_EQ(0, broken_down->tm_sec);
  ASSERT_EQ(0, broken_down->tm_min);
  ASSERT_EQ(0, broken_down->tm_hour);
  ASSERT_EQ(1, broken_down->tm_mday);
  ASSERT_EQ(0, broken_down->tm_mon);
  ASSERT_EQ(1970, broken_down->tm_year + 1900);
}

TEST(time, mktime_10310929) {
  struct tm t;
  memset(&t, 0, sizeof(tm));
  t.tm_year = 200;
  t.tm_mon = 2;
  t.tm_mday = 10;

#if !defined(__LP64__)
  // 32-bit bionic stupidly had a signed 32-bit time_t.
  ASSERT_EQ(-1, mktime(&t));
#if defined(__BIONIC__)
  ASSERT_EQ(-1, mktime_tz(&t, "UTC"));
#endif
#else
  // Everyone else should be using a signed 64-bit time_t.
  ASSERT_GE(sizeof(time_t) * 8, 64U);

  setenv("TZ", "America/Los_Angeles", 1);
  tzset();
  ASSERT_EQ(static_cast<time_t>(4108348800U), mktime(&t));
#if defined(__BIONIC__)
  ASSERT_EQ(static_cast<time_t>(4108320000U), mktime_tz(&t, "UTC"));
#endif

  setenv("TZ", "UTC", 1);
  tzset();
  ASSERT_EQ(static_cast<time_t>(4108320000U), mktime(&t));
#if defined(__BIONIC__)
  ASSERT_EQ(static_cast<time_t>(4108348800U), mktime_tz(&t, "America/Los_Angeles"));
#endif
#endif
}

void SetTime(timer_t t, time_t value_s, time_t value_ns, time_t interval_s, time_t interval_ns) {
  itimerspec ts;
  ts.it_value.tv_sec = value_s;
  ts.it_value.tv_nsec = value_ns;
  ts.it_interval.tv_sec = interval_s;
  ts.it_interval.tv_nsec = interval_ns;
  ASSERT_EQ(0, timer_settime(t, TIMER_ABSTIME, &ts, NULL));
}

static void NoOpNotifyFunction(sigval_t) {
}

TEST(time, timer_create) {
  sigevent_t se;
  memset(&se, 0, sizeof(se));
  se.sigev_notify = SIGEV_THREAD;
  se.sigev_notify_function = NoOpNotifyFunction;
  timer_t timer_id;
  ASSERT_EQ(0, timer_create(CLOCK_MONOTONIC, &se, &timer_id));

  int pid = fork();
  ASSERT_NE(-1, pid) << strerror(errno);

  if (pid == 0) {
    // Timers are not inherited by the child.
    ASSERT_EQ(-1, timer_delete(timer_id));
    ASSERT_EQ(EINVAL, errno);
    _exit(0);
  }

  ASSERT_EQ(0, timer_delete(timer_id));
}

static int timer_create_SIGEV_SIGNAL_signal_handler_invocation_count = 0;
static void timer_create_SIGEV_SIGNAL_signal_handler(int signal_number) {
  ++timer_create_SIGEV_SIGNAL_signal_handler_invocation_count;
  ASSERT_EQ(SIGUSR1, signal_number);
}

TEST(time, timer_create_SIGEV_SIGNAL) {
  sigevent_t se;
  memset(&se, 0, sizeof(se));
  se.sigev_notify = SIGEV_SIGNAL;
  se.sigev_signo = SIGUSR1;

  timer_t timer_id;
  ASSERT_EQ(0, timer_create(CLOCK_MONOTONIC, &se, &timer_id));

  struct sigaction action;
  struct sigaction original_action;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  action.sa_handler = timer_create_SIGEV_SIGNAL_signal_handler;
  ASSERT_EQ(0, sigaction(SIGUSR1, &action, &original_action));

  ASSERT_EQ(0, timer_create_SIGEV_SIGNAL_signal_handler_invocation_count);

  itimerspec ts;
  ts.it_value.tv_sec =  0;
  ts.it_value.tv_nsec = 1;
  ts.it_interval.tv_sec = 0;
  ts.it_interval.tv_nsec = 0;
  ASSERT_EQ(0, timer_settime(timer_id, TIMER_ABSTIME, &ts, NULL));

  sleep(1);
  ASSERT_EQ(0, sigaction(SIGUSR1, &original_action, NULL));
  ASSERT_EQ(1, timer_create_SIGEV_SIGNAL_signal_handler_invocation_count);
}

struct CounterData {
  int counter;
  timer_t timer_id;
  sigevent_t se;

  CounterData() : counter(0) {
    memset(&se, 0, sizeof(se));
    se.sigev_notify = SIGEV_THREAD;
    se.sigev_value.sival_ptr = this;
  }

  static void CountNotifyFunction(sigval_t value) {
    CounterData* cd = reinterpret_cast<CounterData*>(value.sival_ptr);
    ++cd->counter;
  }

  static void CountAndDisarmNotifyFunction(sigval_t value) {
    CounterData* cd = reinterpret_cast<CounterData*>(value.sival_ptr);
    ++cd->counter;

    // Setting the initial expiration time to 0 disarms the timer.
    SetTime(cd->timer_id, 0, 0, 1, 0);
  }
};

TEST(time, timer_settime_0) {
  CounterData counter_data;

  counter_data.se.sigev_notify_function = CounterData::CountAndDisarmNotifyFunction;
  ASSERT_EQ(0, timer_create(CLOCK_REALTIME, &counter_data.se, &counter_data.timer_id));

  ASSERT_EQ(0, counter_data.counter);

  SetTime(counter_data.timer_id, 0, 1, 1, 0);
  sleep(1);

  // The count should just be 1 because we disarmed the timer the first time it fired.
  ASSERT_EQ(1, counter_data.counter);
}

TEST(time, timer_settime_repeats) {
  CounterData counter_data;

  counter_data.se.sigev_notify_function = CounterData::CountNotifyFunction;
  ASSERT_EQ(0, timer_create(CLOCK_REALTIME, &counter_data.se, &counter_data.timer_id));

  ASSERT_EQ(0, counter_data.counter);

  SetTime(counter_data.timer_id, 0, 1, 0, 10);
  sleep(1);

  // The count should just be > 1 because we let the timer repeat.
  ASSERT_GT(counter_data.counter, 1);
}

static int timer_create_NULL_signal_handler_invocation_count = 0;
static void timer_create_NULL_signal_handler(int signal_number) {
  ++timer_create_NULL_signal_handler_invocation_count;
  ASSERT_EQ(SIGALRM, signal_number);
}

TEST(time, timer_create_NULL) {
  // A NULL sigevent* is equivalent to asking for SIGEV_SIGNAL for SIGALRM.
  timer_t timer_id;
  ASSERT_EQ(0, timer_create(CLOCK_MONOTONIC, NULL, &timer_id));

  struct sigaction action;
  struct sigaction original_action;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  action.sa_handler = timer_create_NULL_signal_handler;
  ASSERT_EQ(0, sigaction(SIGALRM, &action, &original_action));

  SetTime(timer_id, 0, 1, 0, 0);
  sleep(1);

  ASSERT_EQ(1, timer_create_NULL_signal_handler_invocation_count);
}
