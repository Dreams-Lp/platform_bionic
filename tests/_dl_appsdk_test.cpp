/*
 * Copyright (C) 2015 The Android Open Source Project
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

extern "C" uint32_t _dl_android_get_application_target_sdk_version();
extern "C" void _dl_android_set_application_target_sdk_version(uint32_t target);

TEST(_dl_appsdk, _dl_application_sdk_versions_smoke) {
  // Check initial values
  ASSERT_EQ(0U, _dl_android_get_application_target_sdk_version());

  _dl_android_set_application_target_sdk_version(20U);
  ASSERT_EQ(20U, _dl_android_get_application_target_sdk_version());

  _dl_android_set_application_target_sdk_version(22U);
  ASSERT_EQ(22U, _dl_android_get_application_target_sdk_version());
}

