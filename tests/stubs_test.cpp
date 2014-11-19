/*
 * Copyright (C) 2012 The Android Open Source Project
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

// Below are the header files we want to test.
#include <grp.h>
#include <pwd.h>

#include <errno.h>
#include <limits.h>
#include <sys/cdefs.h>
#include <sys/types.h>
#include <unistd.h>

#define UNUSED(a) a = a

typedef enum {
  TYPE_SYSTEM,
  TYPE_APP
} uid_type_t;

#if defined(__BIONIC__)

static void check_passwd(const passwd* pwd, const char* username, uid_t uid, uid_type_t uid_type) {
  ASSERT_TRUE(pwd != NULL);
  ASSERT_STREQ(username, pwd->pw_name);
  ASSERT_EQ(uid, pwd->pw_uid);
  ASSERT_EQ(uid, pwd->pw_gid);
  ASSERT_EQ(NULL, pwd->pw_passwd);
#ifdef __LP64__
  ASSERT_EQ(NULL, pwd->pw_gecos);
#endif

  if (uid_type == TYPE_SYSTEM) {
    ASSERT_STREQ("/", pwd->pw_dir);
  } else {
    ASSERT_STREQ("/data", pwd->pw_dir);
  }
  ASSERT_STREQ("/system/bin/sh", pwd->pw_shell);
}

static void check_getpwnam(const char* username, uid_t uid, uid_type_t uid_type) {
  errno = 0;
  passwd* pwd1 = getpwuid(uid);
  ASSERT_EQ(0, errno);
  SCOPED_TRACE("getpwuid");
  check_passwd(pwd1, username, uid, uid_type);

  errno = 0;
  passwd* pwd2 = getpwnam(username);
  ASSERT_EQ(0, errno);
  SCOPED_TRACE("getpwnam");
  check_passwd(pwd2, username, uid, uid_type);

  passwd pwd_st;
  char buf[512];
  int ret;
  errno = 0;
  passwd* pwd3 = NULL;
  ret = getpwnam_r(username, &pwd_st, buf, sizeof(buf), &pwd3);
  ASSERT_EQ(0, ret);
  ASSERT_EQ(0, errno);
  SCOPED_TRACE("getpwnam_r");
  check_passwd(pwd3, username, uid, uid_type);

  errno = 0;
  passwd* pwd4 = NULL;
  ret = getpwuid_r(uid, &pwd_st, buf, sizeof(buf), &pwd4);
  ASSERT_EQ(0, ret);
  ASSERT_EQ(0, errno);
  SCOPED_TRACE("getpwuid_r");
  check_passwd(pwd4, username, uid, uid_type);
}

#else // !defined(__BIONIC__)

static void check_getpwnam(const char* username, uid_t uid, uid_type_t uid_type) {
  UNUSED(username);
  UNUSED(uid);
  UNUSED(uid_type);
  GTEST_LOG_(INFO) << "This test does nothing.\n";
}

#endif

TEST(getpwnam, system_id_root) {
  SCOPED_TRACE("root");
  check_getpwnam("root", 0, TYPE_SYSTEM);
}

TEST(getpwnam, system_id_system) {
  SCOPED_TRACE("system");
  check_getpwnam("system", 1000, TYPE_SYSTEM);
}

TEST(getpwnam, app_id_radio) {
  SCOPED_TRACE("radio");
  check_getpwnam("radio", 1001, TYPE_SYSTEM);
}

TEST(getpwnam, app_id_nobody) {
  SCOPED_TRACE("nobody");
  check_getpwnam("nobody", 9999, TYPE_SYSTEM);
}

TEST(getpwnam, app_id_u0_a0) {
  SCOPED_TRACE("u0_a0");
  check_getpwnam("u0_a0", 10000, TYPE_APP);
}

TEST(getpwnam, app_id_u0_a1234) {
  SCOPED_TRACE("u0_a1234");
  check_getpwnam("u0_a1234", 11234, TYPE_APP);
}

// Test the difference between uid and shared gid.
TEST(getpwnam, app_id_u0_a49999) {
  SCOPED_TRACE("u0_a49999");
  check_getpwnam("u0_a49999", 59999, TYPE_APP);
}

TEST(getpwnam, app_id_u0_i1) {
  SCOPED_TRACE("u0_i1");
  check_getpwnam("u0_i1", 99001, TYPE_APP);
}

TEST(getpwnam, app_id_u1_root) {
  SCOPED_TRACE("u1_root");
  check_getpwnam("u1_root", 100000, TYPE_SYSTEM);
}

TEST(getpwnam, app_id_u1_radio) {
  SCOPED_TRACE("u1_radio");
  check_getpwnam("u1_radio", 101001, TYPE_SYSTEM);
}

TEST(getpwnam, app_id_u1_a0) {
  SCOPED_TRACE("u1_a0");
  check_getpwnam("u1_a0", 110000, TYPE_APP);
}

TEST(getpwnam, app_id_u1_a40000) {
  SCOPED_TRACE("u1_a40000");
  check_getpwnam("u1_a40000", 150000, TYPE_APP);
}

TEST(getpwnam, app_id_u1_i0) {
  SCOPED_TRACE("u1_i0");
  check_getpwnam("u1_i0", 199000, TYPE_APP);
}

#if defined(__BIONIC__)

static void check_group(const group* grp, const char* group_name, gid_t gid) {
  ASSERT_TRUE(grp != NULL);
  ASSERT_STREQ(group_name, grp->gr_name);
  ASSERT_EQ(gid, grp->gr_gid);
  ASSERT_TRUE(grp->gr_mem != NULL);
  ASSERT_STREQ(group_name, grp->gr_mem[0]);
  ASSERT_TRUE(grp->gr_mem[1] == NULL);
}

static void check_getgrnam(const char* group_name, gid_t gid) {
  errno = 0;
  group* grp1 = getgrgid(gid);
  ASSERT_EQ(0, errno);
  SCOPED_TRACE("getgrgid");
  check_group(grp1, group_name, gid);

  errno = 0;
  group* grp2 = getgrnam(group_name);
  ASSERT_EQ(0, errno);
  SCOPED_TRACE("getgrnam");
  check_group(grp2, group_name, gid);
}

#else // !defined(__BIONIC__)

static void check_getgrnam(const char* group_name, gid_t gid) {
  UNUSED(group_name);
  UNUSED(gid);
  GTEST_LOG_(INFO) << "This test does nothing.\n";
}

#endif

TEST(getgrnam, system_id_root) {
  SCOPED_TRACE("root");
  check_getgrnam("root", 0);
}

TEST(getgrnam, system_id_system) {
  SCOPED_TRACE("system");
  check_getgrnam("system", 1000);
}

TEST(getgrnam, app_id_radio) {
  SCOPED_TRACE("radio");
  check_getgrnam("radio", 1001);
}

TEST(getgrnam, app_id_nobody) {
  SCOPED_TRACE("nobody");
  check_getgrnam("nobody", 9999);
}

TEST(getgrnam, app_id_u0_a0) {
  SCOPED_TRACE("u0_a0");
  check_getgrnam("u0_a0", 10000);
}

TEST(getgrnam, app_id_u0_a1234) {
  SCOPED_TRACE("u0_a1234");
  check_getgrnam("u0_a1234", 11234);
}

TEST(getgrnam, app_id_u0_a9999) {
  SCOPED_TRACE("u0_a9999");
  check_getgrnam("u0_a9999", 19999);
}

// Test the difference between uid and shared gid.
TEST(getgrnam, app_id_all_a9999) {
  SCOPED_TRACE("all_a9999");
  check_getgrnam("all_a9999", 59999);
}

TEST(getgrnam, app_id_u0_i1) {
  SCOPED_TRACE("u0_i1");
  check_getgrnam("u0_i1", 99001);
}

TEST(getgrnam, app_id_u1_root) {
  SCOPED_TRACE("u1_root");
  check_getgrnam("u1_root", 100000);
}

TEST(getgrnam, app_id_u1_radio) {
  SCOPED_TRACE("u1_radio");
  check_getgrnam("u1_radio", 101001);
}

TEST(getgrnam, app_id_u1_a0) {
  SCOPED_TRACE("u1_a0");
  check_getgrnam("u1_a0", 110000);
}

TEST(getgrnam, app_id_u1_a40000) {
  SCOPED_TRACE("u1_a40000");
  check_getgrnam("u1_a40000", 150000);
}

TEST(getgrnam, app_id_u1_i0) {
  SCOPED_TRACE("u1_i0");
  check_getgrnam("u1_i0", 199000);
}
