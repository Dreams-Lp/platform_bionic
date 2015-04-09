#
# Copyright (C) 2015 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# -----------------------------------------------------------------------------
# Libraries used to test versioned symbols
# -----------------------------------------------------------------------------
libtest_versioned_uselibv1_src_files := versioned_uselib.cpp

libtest_versioned_uselibv1_shared_libraries := \
    libtest_versioned_libv1

module := libtest_versioned_uselibv1
include $(LOCAL_PATH)/Android.build.testlib.mk

# -----------------------------------------------------------------------------
libtest_versioned_uselibv2_src_files := \
    versioned_uselib.cpp

libtest_versioned_uselibv2_shared_libraries := \
    libtest_versioned_libv2

libtest_versioned_uselibv2_ldflags := \
    -Wl,--version-script,$(LOCAL_PATH)/versioned_uselib.map

module := libtest_versioned_uselibv2
include $(LOCAL_PATH)/Android.build.testlib.mk

# -----------------------------------------------------------------------------
# lib v1 - this one used during static linking but never used in runtime
# which forces libtest_versioned_uselibv1 to use function v1 from
# libtest_versioned_lib.so
# -----------------------------------------------------------------------------
libtest_versioned_libv1_src_files := \
    versioned_lib_v1.cpp

libtest_versioned_libv1_ldflags := \
    -Wl,--version-script,$(LOCAL_PATH)/versioned_lib_v1.map \
    -Wl,-soname,libtest_versioned_lib.so

# TODO: investigate why gcc does not follow .symver directive
libtest_versioned_libv1_clang_target := true

module := libtest_versioned_libv1
include $(LOCAL_PATH)/Android.build.testlib.mk

# -----------------------------------------------------------------------------
# lib v2 - to make libtest_versioned_uselibv2.so use version 2 of versioned_function()
# -----------------------------------------------------------------------------
libtest_versioned_libv2_src_files := \
    versioned_lib_v2.cpp

libtest_versioned_libv2_ldflags := \
    -Wl,--version-script,$(LOCAL_PATH)/versioned_lib_v2.map \
    -Wl,-soname,libtest_versioned_lib.so

# TODO: investigate why gcc does not follow .symver directive
libtest_versioned_libv2_clang_target := true

module := libtest_versioned_libv2
include $(LOCAL_PATH)/Android.build.testlib.mk


# -----------------------------------------------------------------------------
# last version - this one is used at the runtime and export 3 versions
# of versioned_symbol()
# -----------------------------------------------------------------------------
libtest_versioned_lib_src_files := \
    versioned_lib_v3.cpp

libtest_versioned_lib_ldflags := \
    -Wl,--version-script,$(LOCAL_PATH)/versioned_lib_v3.map

# TODO: investigate why gcc does not follow .symver directive
libtest_versioned_lib_clang_target := true

module := libtest_versioned_lib
include $(LOCAL_PATH)/Android.build.testlib.mk

