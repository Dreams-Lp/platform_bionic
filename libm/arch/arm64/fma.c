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

#include "math.h"

double fma(double x, double y, double z)
{
    double r;
    __asm__ __volatile__("fmadd %d0, %d1, %d2, %d3": "=x"(r) : "x"(x), "x"(y), "x"(z));
    return r;
}

float fmaf(float x, float y, float z)
{
    float r;
    __asm__ __volatile__("fmadd %s0, %s1, %s2, %s3": "=x"(r) : "x"(x), "x"(y), "x"(z));
    return r;
}
