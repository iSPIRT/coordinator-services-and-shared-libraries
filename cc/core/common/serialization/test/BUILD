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

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_test")

package(default_visibility = ["//cc:pbs_visibility"])

proto_library(
    name = "serialization_test_proto",
    srcs = ["test.proto"],
)

cc_proto_library(
    name = "test_cc_proto",
    deps = [":serialization_test_proto"],
)

cc_test(
    name = "serialization_test",
    size = "small",
    srcs = ["serialization_test.cc"],
    deps = [
        ":test_cc_proto",
        "//cc/core/common/serialization/src:serialization_lib",
        "//cc/core/interface:type_def_lib",
        "//cc/core/test/utils:utils_lib",
        "@com_google_googletest//:gtest_main",
    ],
)
