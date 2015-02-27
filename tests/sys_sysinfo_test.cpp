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

#include <gtest/gtest.h>

#include <sys/sysinfo.h>

TEST(sys_sysinfo, smoke) {
  int nprocessor = get_nprocs();
  ASSERT_GT(nprocessor, 0);

  int nprocessor_conf = get_nprocs_conf();
  ASSERT_GE(nprocessor_conf, nprocessor);

  long avail_phys_pages = get_avphys_pages();
  ASSERT_GT(avail_phys_pages, 0);

  long phys_pages = get_phys_pages();
  ASSERT_GE(phys_pages, avail_phys_pages);
}

TEST(sys_sysinfo, sysinfo)
{
  struct sysinfo si;

  sysinfo(&si);

  EXPECT_LT(0, si.uptime);
  EXPECT_LE(0, (int)si.loads[0]);
  EXPECT_LE(0, (int)(si.loads[1]));
  EXPECT_LE(0, (int)(si.loads[2]));

  EXPECT_LT(0, (int)(si.totalram));
  EXPECT_LE(0, (int)(si.freeram));
  EXPECT_LE(si.freeram, si.totalram);
  EXPECT_LE(0, (int)(si.sharedram));
  EXPECT_LE(0, (int)(si.bufferram));

  EXPECT_LE(0, (int)(si.totalhigh));
  EXPECT_LE(0, (int)(si.freehigh));
  EXPECT_LE(si.freehigh, si.totalhigh);

  EXPECT_LE(0, (int)(si.totalswap));
  EXPECT_LE(0, (int)(si.freeswap));
  EXPECT_LE(si.freeswap, si.totalswap);

  EXPECT_LT(0, (int)(si.mem_unit));

  EXPECT_LT(0, si.procs);
}
