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

import static com.google.common.collect.ImmutableList.toImmutableList;
import static com.google.common.truth.Truth.assertThat;

import com.google.common.collect.ImmutableList;
import org.apache.avro.Schema;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)
public class AvroResultsSchemaSupplierTest {

  // Under test
  AvroResultsSchemaSupplier avroResultsSchemaSupplier;

  @Before
  public void setUp() {
    avroResultsSchemaSupplier = new AvroResultsSchemaSupplier();
  }

  @Test
  public void testHasCorrectSchema() {
    // no setup

    Schema schema = avroResultsSchemaSupplier.get();

    // Get the list of the field names and check that they are the expected fields
    ImmutableList<String> fieldNames =
        schema.getFields().stream().map(Schema.Field::name).collect(toImmutableList());
    assertThat(fieldNames).containsExactly("key", "value");
  }

  @Test
  public void testSchemaIsCached() {
    // no setup

    Schema firstGet = avroResultsSchemaSupplier.get();
    Schema secondGet = avroResultsSchemaSupplier.get();

    assertThat(firstGet).isSameInstanceAs(secondGet);
  }
}
