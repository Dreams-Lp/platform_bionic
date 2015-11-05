/*
 * Copyright (C) 2013 The Android Open Source Project
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

#include "private/bionic_time_conversions.h"

#include "private/bionic_constants.h"

bool timespec_from_timeval(timespec& ts, const timeval& tv) {
  // Whole seconds can just be copied.
  ts.tv_sec = tv.tv_sec;

  // But we might overflow when converting microseconds to nanoseconds.
  if (tv.tv_usec >= 1000000 || tv.tv_usec < 0) {
    return false;
  }
  ts.tv_nsec = tv.tv_usec * 1000;
  return true;
}

void timespec_from_ms(timespec& ts, const int ms) {
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000;
}

void timeval_from_timespec(timeval& tv, const timespec& ts) {
  tv.tv_sec = ts.tv_sec;
  tv.tv_usec = ts.tv_nsec / 1000;
}

// Initializes 'ts' with the difference between 'abs_ts' and the current time
// according to 'clock'. Returns false if abstime already expired, true otherwise.
bool timespec_from_absolute_timespec(timespec& ts, const timespec& abs_ts, clockid_t clock) {
  clock_gettime(clock, &ts);
  ts.tv_sec = abs_ts.tv_sec - ts.tv_sec;
  ts.tv_nsec = abs_ts.tv_nsec - ts.tv_nsec;
  if (ts.tv_nsec < 0) {
    ts.tv_sec--;
    ts.tv_nsec += NS_PER_S;
  }
  if (ts.tv_nsec < 0 || ts.tv_sec < 0) {
    return false;
  }
  return true;
}

void absolute_timespec_from_timespec(timespec& abs_ts, const timespec& ts, clockid_t clock) {
  clock_gettime(clock, &abs_ts);
  abs_ts.tv_sec += ts.tv_sec;
  abs_ts.tv_nsec += ts.tv_nsec;
  if (abs_ts.tv_nsec >= NS_PER_S) {
    abs_ts.tv_nsec -= NS_PER_S;
    abs_ts.tv_sec++;
  }
}
