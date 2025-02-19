/*
 * Copyright 2025 Google LLC
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

package com.google.scp.protocol.avro;

import javax.inject.Singleton;

/** Supplies the Avro {@code Schema} for the results file. */
@Singleton
public final class AvroResultsSchemaSupplier extends AvroSchemaSupplier {

  @Override
  String schemaResourcePath() {
    return "/operator/protocol/avro/results.avsc";
  }
}
