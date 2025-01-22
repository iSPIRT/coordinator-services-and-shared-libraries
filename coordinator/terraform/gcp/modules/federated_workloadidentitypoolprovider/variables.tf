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

variable "project_id" {
  description = "GCP Project ID in which workload identity pool (wip) and workload identity pool provider (wipp) will be created in."
  type        = string
}

variable "environment" {
  description = "Description for the environment, e.g. dev, staging, production."
  type        = string
}

variable "aws_account_id_to_role_names" {
  description = <<EOT
    A map of AWS account IDs to a list of roles within that account.
    Only the roles in the list will be given access to private key endpoint and attestation.
    If list of roles is empty the whole account is given access.
  EOT
  type        = map(set(string))
}

variable "wip_allowed_service_account_project_id_override" {
  description = "GCP Project ID in which wip allowed service account will be created in."
  type        = string
}

variable "enable_audit_log" {
  description = "Enable Data Access Audit Logging for projects containing the WIP and Service Accounts."
  type        = bool
}
