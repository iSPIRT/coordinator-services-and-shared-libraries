// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cc/pbs/front_end_service/src/front_end_service_v2.h"

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_format.h"
#include "cc/core/common/global_logger/src/global_logger.h"
#include "cc/core/common/uuid/src/uuid.h"
#include "cc/core/interface/async_context.h"
#include "cc/core/interface/async_executor_interface.h"
#include "cc/core/interface/config_provider_interface.h"
#include "cc/core/interface/errors.h"
#include "cc/core/interface/http_server_interface.h"
#include "cc/core/interface/http_types.h"
#include "cc/core/interface/logger_interface.h"
#include "cc/core/interface/type_def.h"
#include "cc/core/utils/src/http.h"
#include "cc/pbs/consume_budget/src/gcp/error_codes.h"
#include "cc/pbs/front_end_service/src/error_codes.h"
#include "cc/pbs/front_end_service/src/front_end_utils.h"
#include "cc/pbs/interface/configuration_keys.h"
#include "cc/pbs/interface/consume_budget_interface.h"
#include "cc/pbs/interface/front_end_service_interface.h"
#include "cc/pbs/interface/type_def.h"
#include "cc/public/core/interface/execution_result.h"

namespace google::scp::pbs {
namespace {

using ::google::scp::core::AsyncContext;
using ::google::scp::core::AsyncExecutorInterface;
using ::google::scp::core::ExecutionResult;
using ::google::scp::core::ExecutionResultOr;
using ::google::scp::core::FailureExecutionResult;
using ::google::scp::core::HttpHandler;
using ::google::scp::core::HttpHeaders;
using ::google::scp::core::HttpMethod;
using ::google::scp::core::HttpRequest;
using ::google::scp::core::HttpResponse;
using ::google::scp::core::HttpServerInterface;
using ::google::scp::core::SuccessExecutionResult;
using ::google::scp::core::Timestamp;
using ::google::scp::core::common::kZeroUuid;
using ::google::scp::core::common::ToString;
using ::google::scp::core::common::Uuid;
using ::google::scp::core::errors::
    SC_PBS_FRONT_END_SERVICE_GET_TRANSACTION_STATUS_RETURNS_404_BY_DEFAULT;
using ::google::scp::core::errors::
    SC_PBS_FRONT_END_SERVICE_INITIALIZATION_FAILED;
using ::google::scp::core::errors::SC_PBS_FRONT_END_SERVICE_INVALID_REQUEST;
using ::google::scp::pbs::FrontEndUtils;

constexpr char kFrontEndService[] = "FrontEndServiceV2";
constexpr char kTransactionLastExecutionTimestampHeader[] =
    "x-gscp-transaction-last-execution-timestamp";
constexpr char kFakeLastExecutionTimestamp[] = "1234";

// Considering an estimated load of 75 keys per transaction with a standard
// deviation of 20.
// The maximum number of keys is approximately 20,000.
constexpr std::array<double, 26> kKeysBoundaries = {
    1.0,    1.5,    2.3,    3.4,    5.1,    7.6,     11.4,    17.1,   25.6,
    38.4,   57.7,   86.5,   129.7,  194.6,  291.9,   437.9,   656.8,  985.3,
    1477.9, 2216.8, 3325.3, 4987.9, 7481.8, 11222.7, 16864.1, 25251.2};

// The extracted transaction_id is unused in BeginTransaction of
// front_end_service_v2.cc, but the extraction serves two purposes:
// 1. Make sure that the client is continues to adhere to the client server
//    interaction contract that was previously enforced by
//    front_end_service.cc
// 2. Make sure that the transaction ID in the header can be extracted without
//    any error. If the transaction ID can be extracted in BeginTransaction,
//    it is likely that it can also be extracted in PrepareTransaction, so it
//    helps PBS detect potential extraction issue earlier.
// The same reasoning applies to other phases.
//
// The transaction_id will be returned if there isn't any extraction issue. The
// transaction_id will only be used for logging purposes.
ExecutionResultOr<std::string> ExtractTransactionId(
    const AsyncContext<HttpRequest, HttpResponse>& http_context) {
  if (http_context.request == nullptr ||
      http_context.request->headers == nullptr) {
    return FailureExecutionResult(SC_PBS_FRONT_END_SERVICE_INVALID_REQUEST);
  }

  Uuid transaction_id;
  if (auto execution_result = FrontEndUtils::ExtractTransactionId(
          http_context.request->headers, transaction_id);
      !execution_result.Successful()) {
    return execution_result;
  }
  return ToString(transaction_id);
}

// The last execution timestamp was used in front_end_service.cc to support
// optimistic concurrency control in two-phase commit transaction.
// front_end_service_v2.cc does not support such concurrency control, but it
// is still returning this header to the client for backward compatibility
// purpose.
void InsertBackwardCompatibleHeaders(
    AsyncContext<HttpRequest, HttpResponse>& http_context) {
  http_context.response->headers = std::make_shared<HttpHeaders>();
  http_context.response->headers->insert(
      {kTransactionLastExecutionTimestampHeader, kFakeLastExecutionTimestamp});
}
}  // namespace

FrontEndServiceV2::FrontEndServiceV2(
    std::shared_ptr<core::HttpServerInterface> http_server,
    std::shared_ptr<core::AsyncExecutorInterface> async_executor,
    const std::shared_ptr<core::ConfigProviderInterface> config_provider,
    BudgetConsumptionHelperInterface* budget_consumption_helper,
    core::MetricRouter* metric_router)
    : http_server_(http_server),
      async_executor_(async_executor),
      config_provider_(config_provider),
      budget_consumption_helper_(budget_consumption_helper),
      metric_router_(metric_router) {
  MetricInit();
}

void FrontEndServiceV2::MetricInit() noexcept {
  if (!metric_router_) {
    return;
  }
  meter_ = metric_router_->GetOrCreateMeter(kFrontEndServiceV2Meter);

  metric_router_->CreateViewForInstrument(
      /*meter_name=*/kFrontEndServiceV2Meter,
      /*instrument_name=*/kKeysPerTransaction,
      /*instrument_type=*/
      opentelemetry::sdk::metrics::InstrumentType::kHistogram,
      /*aggregation_type=*/
      opentelemetry::sdk::metrics::AggregationType::kHistogram,
      /*boundaries=*/kKeysBoundaries,
      /*version=*/"", /*schema=*/"",
      /*view_description=*/
      "Number of keys/budgets per transaction/job histogram",
      /*unit=*/"");

  keys_per_transaction_count_ =
      std::static_pointer_cast<opentelemetry::metrics::Histogram<uint64_t>>(
          metric_router_->GetOrCreateSyncInstrument(
              kKeysPerTransaction,
              [&]() -> std::shared_ptr<
                        opentelemetry::metrics::SynchronousInstrument> {
                return meter_->CreateUInt64Histogram(
                    kKeysPerTransaction,
                    "Number of keys/budgets per transaction/job");
              }));

  metric_router_->CreateViewForInstrument(
      /*meter_name=*/kFrontEndServiceV2Meter,
      /*instrument_name=*/kSuccessfulBudgetConsumed,
      /*instrument_type=*/
      opentelemetry::sdk::metrics::InstrumentType::kHistogram,
      /*aggregation_type=*/
      opentelemetry::sdk::metrics::AggregationType::kHistogram,
      /*boundaries=*/kKeysBoundaries,
      /*version=*/"", /*schema=*/"",
      /*view_description=*/
      "Number of successful budgets consumed in a transaction/job histogram",
      /*unit=*/"");

  successful_budget_consumed_counter_ = std::static_pointer_cast<
      opentelemetry::metrics::Histogram<uint64_t>>(
      metric_router_->GetOrCreateSyncInstrument(
          kSuccessfulBudgetConsumed,
          [&]() -> std::shared_ptr<
                    opentelemetry::metrics::SynchronousInstrument> {
            return meter_->CreateUInt64Histogram(
                kSuccessfulBudgetConsumed,
                "Number of successful budgets consumed in a transaction/job");
          }));

  // Budget Exhausted View Setup.
  static std::vector<double> kBudgetExhaustedBoundaries = {
      1.0,  2.0,   4.0,   8.0,   16.0,   32.0,
      64.0, 128.0, 256.0, 512.0, 1024.0, 2048.0};

  metric_router_->CreateViewForInstrument(
      /*meter_name=*/kFrontEndServiceV2Meter,
      /*instrument_name=*/kBudgetExhausted,
      /*instrument_type=*/
      opentelemetry::sdk::metrics::InstrumentType::kHistogram,
      /*aggregation_type=*/
      opentelemetry::sdk::metrics::AggregationType::kHistogram,
      /*boundaries=*/kBudgetExhaustedBoundaries,
      /*version=*/"", /*schema=*/"",
      /*view_description=*/"Number of budgets exhausted",
      /*unit=*/"");

  budgets_exhausted_ =
      std::static_pointer_cast<opentelemetry::metrics::Histogram<uint64_t>>(
          metric_router_->GetOrCreateSyncInstrument(
              kBudgetExhausted,
              [&]() -> std::shared_ptr<
                        opentelemetry::metrics::SynchronousInstrument> {
                return meter_->CreateUInt64Histogram(
                    kBudgetExhausted, "Number of budgets exhausted");
              }));
}

ExecutionResult FrontEndServiceV2::Init() noexcept {
  ExecutionResult execution_result =
      config_provider_->Get(kRemotePrivacyBudgetServiceClaimedIdentity,
                            remote_coordinator_claimed_identity_);

  RETURN_IF_FAILURE(execution_result);

  std::string health_check_path(kStatusHealthCheckPath);
  HttpHandler health_check_handler =
      std::bind_front(&FrontEndServiceV2::BeginTransaction, this);
  http_server_->RegisterResourceHandler(HttpMethod::POST, health_check_path,
                                        health_check_handler);

  std::string consume_budget_path(kStatusConsumeBudgetPath);
  HttpHandler consume_budget_handler =
      std::bind_front(&FrontEndServiceV2::PrepareTransaction, this);
  http_server_->RegisterResourceHandler(HttpMethod::POST, consume_budget_path,
                                        consume_budget_handler);

  std::string begin_transaction_path(kBeginTransactionPath);
  HttpHandler begin_transaction_handler =
      std::bind_front(&FrontEndServiceV2::BeginTransaction, this);
  http_server_->RegisterResourceHandler(
      HttpMethod::POST, begin_transaction_path, begin_transaction_handler);

  std::string prepare_transaction_path(kPrepareTransactionPath);
  HttpHandler prepare_transaction_handler =
      std::bind_front(&FrontEndServiceV2::PrepareTransaction, this);
  http_server_->RegisterResourceHandler(
      HttpMethod::POST, prepare_transaction_path, prepare_transaction_handler);

  std::string commit_transaction_path(kCommitTransactionPath);
  HttpHandler commit_transaction_handler =
      std::bind_front(&FrontEndServiceV2::CommitTransaction, this);
  http_server_->RegisterResourceHandler(
      HttpMethod::POST, commit_transaction_path, commit_transaction_handler);

  std::string notify_transaction_path(kNotifyTransactionPath);
  HttpHandler notify_transaction_handler =
      std::bind_front(&FrontEndServiceV2::NotifyTransaction, this);
  http_server_->RegisterResourceHandler(
      HttpMethod::POST, notify_transaction_path, notify_transaction_handler);

  std::string abort_transaction_path(kAbortTransactionPath);
  HttpHandler abort_transaction_handler =
      std::bind_front(&FrontEndServiceV2::AbortTransaction, this);
  http_server_->RegisterResourceHandler(
      HttpMethod::POST, abort_transaction_path, abort_transaction_handler);

  std::string end_transaction_path(kEndTransactionPath);
  HttpHandler end_transaction_handler =
      std::bind_front(&FrontEndServiceV2::EndTransaction, this);
  http_server_->RegisterResourceHandler(HttpMethod::POST, end_transaction_path,
                                        end_transaction_handler);

  std::string get_transaction_status_path(kStatusTransactionPath);
  HttpHandler get_transaction_transaction_handler =
      std::bind_front(&FrontEndServiceV2::GetTransactionStatus, this);
  http_server_->RegisterResourceHandler(HttpMethod::GET,
                                        get_transaction_status_path,
                                        get_transaction_transaction_handler);

  if (budget_consumption_helper_ == nullptr) {
    auto failure_execution_result =
        FailureExecutionResult(SC_PBS_FRONT_END_SERVICE_INITIALIZATION_FAILED);
    SCP_ERROR(kFrontEndService, kZeroUuid, failure_execution_result,
              "BudgetConsumptionHelper is nullptr during initialization of "
              "FrontEndServiceV2.");
    return failure_execution_result;
  }

  return SuccessExecutionResult();
}

ExecutionResult FrontEndServiceV2::Run() noexcept {
  return SuccessExecutionResult();
}

ExecutionResult FrontEndServiceV2::Stop() noexcept {
  return SuccessExecutionResult();
}

std::shared_ptr<std::string> FrontEndServiceV2::ObtainTransactionOrigin(
    AsyncContext<HttpRequest, HttpResponse>& http_context) const {
  // If transaction origin is supplied in the header use that instead. The
  // transaction origin in the header is useful if a peer coordinator is
  // resolving a transaction on behalf of a client.
  std::string transaction_origin_in_header;
  auto execution_result = FrontEndUtils::ExtractTransactionOrigin(
      http_context.request->headers, transaction_origin_in_header);
  if (execution_result.Successful() && !transaction_origin_in_header.empty()) {
    return std::make_shared<std::string>(
        std::move(transaction_origin_in_header));
  }
  return http_context.request->auth_context.authorized_domain;
}

ExecutionResult FrontEndServiceV2::ExecuteConsumeBudgetTransaction(
    AsyncContext<ConsumeBudgetTransactionRequest,
                 ConsumeBudgetTransactionResponse>&
        consume_budget_transaction_context) noexcept {
  // No-op. This method is unused.
  return SuccessExecutionResult();
}

ExecutionResult FrontEndServiceV2::BeginTransaction(
    AsyncContext<HttpRequest, HttpResponse>& http_context) noexcept {
  SCP_DEBUG_CONTEXT(kFrontEndService, http_context, "Start BeginTransaction.");
  ExecutionResultOr<std::string> transaction_id =
      ExtractTransactionId(http_context);
  if (!transaction_id.result().Successful()) {
    return transaction_id.result();
  }

  InsertBackwardCompatibleHeaders(http_context);
  http_context.result = SuccessExecutionResult();
  http_context.Finish();

  return SuccessExecutionResult();
}

ExecutionResult FrontEndServiceV2::PrepareTransaction(
    AsyncContext<HttpRequest, HttpResponse>& http_context) noexcept {
  SCP_DEBUG_CONTEXT(kFrontEndService, http_context,
                    "Start PrepareTransaction.");
  const std::string reporting_origin_metric_label =
      FrontEndUtils::GetReportingOriginMetricLabel(
          http_context.request, remote_coordinator_claimed_identity_);

  absl::flat_hash_map<absl::string_view, std::string> labels = {
      {kMetricLabelTransactionPhase, kMetricLabelPrepareTransaction},
      {kMetricLabelKeyReportingOrigin, reporting_origin_metric_label},
      {core::kPbsClaimedIdentityLabel,
       core::utils::GetClaimedIdentityOrUnknownValue(http_context)},
      {core::kScpHttpRequestClientVersionLabel,
       core::utils::GetUserAgentOrUnknownValue(http_context)}};

  if (std::string* auth_domain =
          http_context.request->auth_context.authorized_domain.get();
      auth_domain != nullptr) {
    labels.try_emplace(core::kPbsAuthDomainLabel, *auth_domain);
  }

  ExecutionResultOr<std::string> transaction_id =
      ExtractTransactionId(http_context);
  if (!transaction_id.result().Successful()) {
    return transaction_id.result();
  }

  AsyncContext<ConsumeBudgetsRequest, ConsumeBudgetsResponse>
      consume_budget_context(
          std::make_shared<ConsumeBudgetsRequest>(),
          std::bind_front(&FrontEndServiceV2::OnConsumeBudgetCallback, this,
                          http_context, *transaction_id),
          http_context);
  consume_budget_context.response = std::make_shared<ConsumeBudgetsResponse>();
  auto transaction_origin = ObtainTransactionOrigin(http_context);
  if (auto execution_result = ParseBeginTransactionRequestBody(
          *http_context.request->auth_context.authorized_domain,
          *transaction_origin, http_context.request->body,
          consume_budget_context.request->budgets);
      !execution_result.Successful()) {
    return execution_result;
  }

  if (keys_per_transaction_count_) {
    opentelemetry::context::Context context;
    keys_per_transaction_count_->Record(
        consume_budget_context.request->budgets.size(), labels, context);
  }
  if (consume_budget_context.request->budgets.size() == 0) {
    return FailureExecutionResult(
        core::errors::SC_PBS_FRONT_END_SERVICE_NO_KEYS_AVAILABLE);
  }

  if (google::scp::core::common::GlobalLogger::GetGlobalLogger() &&
      google::scp::core::common::GlobalLogger::IsLogLevelEnabled(
          google::scp::core::LogLevel::kDebug)) {
    SCP_DEBUG_CONTEXT(
        kFrontEndService, http_context,
        absl::StrFormat("Starting Transaction: %s Total Keys: %lld",
                        transaction_id->c_str(),
                        consume_budget_context.request->budgets.size()),
        transaction_id->c_str(),
        consume_budget_context.request->budgets.size());

    for (const auto& consume_budget_metadata :
         consume_budget_context.request->budgets) {
      SCP_DEBUG_CONTEXT(
          kFrontEndService, http_context,
          absl::StrFormat("Transaction: %s Budget Key: %s Reporting Time "
                          "Bucket: %llu Token "
                          "Count: %d",
                          transaction_id->c_str(),
                          consume_budget_metadata.budget_key_name->c_str(),
                          consume_budget_metadata.time_bucket,
                          consume_budget_metadata.token_count),
          transaction_id->c_str(),
          consume_budget_metadata.budget_key_name->c_str(),
          consume_budget_metadata.time_bucket,
          consume_budget_metadata.token_count);
    }
  }

  // ConsumeBudget call failed. Note this is an async function which schedules
  // to consume the budgets and returns immediately
  if (auto execution_result =
          budget_consumption_helper_->ConsumeBudgets(consume_budget_context);
      !execution_result.Successful()) {
    return execution_result;
  }
  return SuccessExecutionResult();
}

void FrontEndServiceV2::OnConsumeBudgetCallback(
    AsyncContext<HttpRequest, HttpResponse> http_context,
    std::string transaction_id,
    AsyncContext<ConsumeBudgetsRequest, ConsumeBudgetsResponse>&
        consume_budget_context) {
  const std::string reporting_origin_metric_label =
      FrontEndUtils::GetReportingOriginMetricLabel(
          http_context.request, remote_coordinator_claimed_identity_);

  absl::flat_hash_map<absl::string_view, std::string> labels = {
      {kMetricLabelTransactionPhase, kMetricLabelPrepareTransaction},
      {kMetricLabelKeyReportingOrigin, reporting_origin_metric_label},
      {core::kPbsClaimedIdentityLabel,
       core::utils::GetClaimedIdentityOrUnknownValue(http_context)},
      {core::kScpHttpRequestClientVersionLabel,
       core::utils::GetUserAgentOrUnknownValue(http_context)}};

  if (std::string* auth_domain =
          http_context.request->auth_context.authorized_domain.get();
      auth_domain != nullptr) {
    labels.try_emplace(core::kPbsAuthDomainLabel, *auth_domain);
  }

  if (!consume_budget_context.result.Successful()) {
    if (consume_budget_context.result.status_code ==
        errors::SC_CONSUME_BUDGET_EXHAUSTED) {
      SCP_WARNING_CONTEXT(
          kFrontEndService, http_context,
          absl::StrFormat("Failed to consume budget due to budget exhausted. "
                          "transaction_id: %s. execution_result: %s",
                          transaction_id,
                          google::scp::core::errors::GetErrorMessage(
                              consume_budget_context.result.status_code)));
      std::list<size_t> budget_exhausted_indices(
          consume_budget_context.response->budget_exhausted_indices.begin(),
          consume_budget_context.response->budget_exhausted_indices.end());
      auto serialization_execution_result =
          FrontEndUtils::SerializeTransactionFailedCommandIndicesResponse(
              budget_exhausted_indices, http_context.response->body);
      if (!serialization_execution_result.Successful()) {
        // We can log it but should not update the error code getting back
        // to the client since it will make it confusing for the proper
        // diagnosis on the transaction execution errors.
        //
        // This behavior is consistent with front_end_service.cc
        SCP_ERROR_CONTEXT(
            kFrontEndService, http_context, serialization_execution_result,
            absl::StrFormat("Serialization of the transaction response failed. "
                            "transaction_id: %s.",
                            transaction_id.c_str()),
            transaction_id.c_str());
      }
      // Count number of budgets exhausted.
      if (budgets_exhausted_) {
        opentelemetry::context::Context context;
        budgets_exhausted_->Record(budget_exhausted_indices.size(), labels,
                                   context);
      }
    } else {
      SCP_ERROR_CONTEXT(
          kFrontEndService, http_context, consume_budget_context.result,
          absl::StrFormat("Failed to consume budget. transaction_id: %s.",
                          transaction_id.c_str()),
          transaction_id.c_str());
    }

    http_context.result = consume_budget_context.result;
    http_context.Finish();
    return;
  }
  // Consumed all the budgets successfully.
  if (successful_budget_consumed_counter_) {
    opentelemetry::context::Context context;
    successful_budget_consumed_counter_->Record(
        consume_budget_context.request->budgets.size(), labels, context);
  }

  InsertBackwardCompatibleHeaders(http_context);
  http_context.result = consume_budget_context.result;
  http_context.Finish();
}

[[deprecated(
    "No longer needed in the new version of PBS and will be removed when "
    "clients can no longer rely on this.")]]
ExecutionResult FrontEndServiceV2::CommitTransaction(
    AsyncContext<HttpRequest, HttpResponse>& http_context) noexcept {
  SCP_DEBUG_CONTEXT(kFrontEndService, http_context, "Start CommitTransaction.");
  ExecutionResultOr<std::string> transaction_id =
      ExtractTransactionId(http_context);
  if (!transaction_id.result().Successful()) {
    return transaction_id.result();
  }

  InsertBackwardCompatibleHeaders(http_context);
  http_context.result = SuccessExecutionResult();
  http_context.Finish();

  return SuccessExecutionResult();
}

[[deprecated(
    "No longer needed in the new version of PBS and will be removed when "
    "clients can no longer rely on this.")]]
ExecutionResult FrontEndServiceV2::NotifyTransaction(
    AsyncContext<HttpRequest, HttpResponse>& http_context) noexcept {
  SCP_DEBUG_CONTEXT(kFrontEndService, http_context, "Start NotifyTransaction.");
  ExecutionResultOr<std::string> transaction_id =
      ExtractTransactionId(http_context);
  if (!transaction_id.result().Successful()) {
    return transaction_id.result();
  }

  InsertBackwardCompatibleHeaders(http_context);
  http_context.result = SuccessExecutionResult();
  http_context.Finish();

  return SuccessExecutionResult();
}

[[deprecated(
    "No longer needed in the new version of PBS and will be removed when "
    "clients can no longer rely on this.")]] ExecutionResult
FrontEndServiceV2::AbortTransaction(
    AsyncContext<HttpRequest, HttpResponse>& http_context) noexcept {
  SCP_DEBUG_CONTEXT(kFrontEndService, http_context, "Start AbortTransaction.");
  ExecutionResultOr<std::string> transaction_id =
      ExtractTransactionId(http_context);
  if (!transaction_id.result().Successful()) {
    return transaction_id.result();
  }

  InsertBackwardCompatibleHeaders(http_context);
  http_context.result = SuccessExecutionResult();
  http_context.Finish();

  return SuccessExecutionResult();
}

[[deprecated(
    "No longer needed in the new version of PBS and will be removed when "
    "clients can no longer rely on this.")]] ExecutionResult
FrontEndServiceV2::EndTransaction(
    AsyncContext<HttpRequest, HttpResponse>& http_context) noexcept {
  SCP_DEBUG_CONTEXT(kFrontEndService, http_context, "Start EndTransaction.");
  ExecutionResultOr<std::string> transaction_id =
      ExtractTransactionId(http_context);
  if (!transaction_id.result().Successful()) {
    return transaction_id.result();
  }

  InsertBackwardCompatibleHeaders(http_context);
  http_context.result = SuccessExecutionResult();
  http_context.Finish();

  return SuccessExecutionResult();
}

[[deprecated(
    "No longer needed in the new version of PBS and will be removed when "
    "clients can no longer rely on this.")]] ExecutionResult
FrontEndServiceV2::GetTransactionStatus(
    AsyncContext<HttpRequest, HttpResponse>& http_context) noexcept {
  SCP_DEBUG_CONTEXT(kFrontEndService, http_context,
                    "Start GetTransactionStatus.");
  return FailureExecutionResult(
      SC_PBS_FRONT_END_SERVICE_GET_TRANSACTION_STATUS_RETURNS_404_BY_DEFAULT);
}
}  // namespace google::scp::pbs
