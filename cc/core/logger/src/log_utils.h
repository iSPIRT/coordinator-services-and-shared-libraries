/*
 * Copyright 2022 Google LLC
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

#pragma once

#include <string>

#include "cc/core/logger/interface/log_provider_interface.h"

namespace google::scp::core::logger {

/// Returns a string representation for LogLevel
std::string ToString(const LogLevel& level);

LogLevel FromString(const std::string& level);

std::string operator+(const LogLevel& level, const std::string& text);

std::string operator+(const std::string& text, const LogLevel& level);

}  // namespace google::scp::core::logger
