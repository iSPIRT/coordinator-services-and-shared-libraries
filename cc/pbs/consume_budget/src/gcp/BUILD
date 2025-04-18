# Copyright 2024 Google LLC
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
load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//cc:pbs_visibility"])

cc_library(
    name = "consume_budget",
    srcs = ["consume_budget.cc"],
    hdrs = ["consume_budget.h"],
    deps = [
        ":error_codes",
        "//cc/core/async_executor/src:core_async_executor_lib",
        "//cc/core/interface:async_context_lib",
        "//cc/core/interface:service_interface_lib",
        "//cc/pbs/budget_key_timeframe_manager/src:pbs_budget_key_timeframe_manager_lib",
        "//cc/pbs/interface:pbs_interface_lib",
        "//cc/pbs/proto/storage:budget_value_cc_proto",
        "//cc/public/core/interface:errors",
        "//cc/public/core/interface:execution_result",
        "@com_github_googleapis_google_cloud_cpp//:spanner",
        "@com_github_nlohmann_json//:singleheader-json",
    ],
)

cc_library(
    name = "error_codes",
    hdrs = ["error_codes.h"],
    deps = [
        "//cc/core/interface:interface_lib",
        "//cc/public/core/interface:execution_result",
    ],
)
