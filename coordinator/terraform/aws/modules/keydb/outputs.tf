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

output "key_db_arn" {
  description = "The ARN of the DynamoDB table"
  value       = aws_dynamodb_table.keydb.arn
}

output "key_db_name" {
  description = "The name of the DynamoDB table"
  value       = aws_dynamodb_table.keydb.name
}

output "key_db_region" {
  description = "The primary region of the DynamoDB table"
  value       = data.aws_region.current.name
}
