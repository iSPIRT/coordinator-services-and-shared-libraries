# Copyright 2022 Google LLC
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

load("@rules_cc//cc:defs.bzl", "cc_test")

package(default_visibility = ["//visibility:private"])

cc_test(
    name = "daemonizer_test",
    size = "small",
    srcs = ["daemonizer_test.cc"],
    deps = [
        "//cc/core/test/utils:utils_lib",
        "//cc/process_launcher/daemonizer/src:daemonizer_lib",
        "//cc/public/core/test/interface:execution_result_matchers",
        "@com_github_nlohmann_json//:singleheader-json",
        "@com_google_googletest//:gtest_main",
    ],
)
