/*
 * Copyright 2015, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>

typedef
union __float64
{
    double d;
    uint64_t u64;
    int64_t s64;
} __float64;

// getHighAbs returns the high 32 bit of double input's absolute value.
static inline
int32_t getHighAbs(double x) {
    __float64 u = { .d = x };
    return (int32_t)((u.s64 & 0x7fffffff00000000ULL) >> 32);
}
