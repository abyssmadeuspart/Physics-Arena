#include "json_visual_adapter.h"

#include <nlohmann/json.hpp>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace physics_arena
{
namespace
{
ArenaStatus ProofError(StatusRecord* error, ArenaStatus status, std::string_view detail)
{
	*error = {};
	const std::string_view component = "visual_run";
	const std::string_view statusText = ArenaStatusText(status);
	std::copy(component.begin(), component.end(), error->component.begin());
	error->componentSize = static_cast<std::uint32_t>(component.size());
	std::copy(statusText.begin(), statusText.end(), error->status.begin());
	error->statusSize = static_cast<std::uint32_t>(statusText.size());
	if (detail.size() > error->detail.size())
		detail = "visual_detail_capacity";
	std::copy(detail.begin(), detail.end(), error->detail.begin());
	error->detailSize = static_cast<std::uint32_t>(detail.size());
	error->code = status;
	return status;
}

ArenaStatus ReadProofFile(const wchar_t* path, std::array<char, 65536>* bytes, std::uint32_t* size, StatusRecord* error)
{
	*size = 0;
	HANDLE file =
	    CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return ProofError(error, ArenaStatus_RunFailed, "visual_proof_missing");
	LARGE_INTEGER length = {};
	DWORD read = 0;
	const int valid = GetFileSizeEx(file, &length) != 0 && length.QuadPart > 0 &&
	                  length.QuadPart < static_cast<LONGLONG>(bytes->size()) &&
	                  ReadFile(file, bytes->data(), static_cast<DWORD>(length.QuadPart), &read, nullptr) != 0 &&
	                  read == static_cast<DWORD>(length.QuadPart);
	CloseHandle(file);
	if (valid == 0)
		return ProofError(error, ArenaStatus_InvalidResult, "visual_proof_read");
	*size = read;
	return ArenaStatus_Ok;
}
} // namespace

ArenaStatus LoadVisualProof(const wchar_t* path, const Catalog* catalog, const CaseRecord& benchmarkCase,
                            std::uint32_t expectedSteps, VisualProofRecord* proof, StatusRecord* error)
{
	if (catalog == nullptr)
		return ProofError(error, ArenaStatus_InvalidArgument, "visual_proof_argument");
	const int qualityOnly = benchmarkCase.fixtureKind == CaseFixtureKind_RagdollStairTumble;
	*proof = {};
	std::array<char, 65536> bytes = {};
	std::uint32_t size = 0;
	if (ReadProofFile(path, &bytes, &size, error) != ArenaStatus_Ok)
		return error->code;
	try
	{
		const nlohmann::ordered_json document = nlohmann::ordered_json::parse(bytes.data(), bytes.data() + size);
		const int schemaVersion = document.value("schema_version", 0);
		if (!document.is_object() || schemaVersion != 4 || document.value("run_status", "") != "ok" ||
		    document.value("visual_validation_status", "") != "ok" || !document.contains("completed_work_unit_count") ||
		    !document["completed_work_unit_count"].is_number_unsigned() || !document.contains("workload_elapsed_ms") ||
		    (qualityOnly != 0 ? !document["workload_elapsed_ms"].is_null()
		                      : !document["workload_elapsed_ms"].is_number()) ||
		    !document.contains("render_elapsed_ms") ||
		    (qualityOnly != 0 ? !document["render_elapsed_ms"].is_null() : !document["render_elapsed_ms"].is_number()))
			return ProofError(error, ArenaStatus_InvalidResult, "visual_proof_contract");
		proof->completedStepCount = document["completed_work_unit_count"].get<std::uint32_t>();
		if (qualityOnly == 0)
		{
			proof->physicsTotalMs = document["workload_elapsed_ms"].get<double>();
			proof->renderTotalMs = document["render_elapsed_ms"].get<double>();
		}
		constexpr std::array<std::string_view, 20> keys = {"schema_version",
		                                                   "run_status",
		                                                   "visual_validation_status",
		                                                   "work_unit_id",
		                                                   "completed_work_unit_count",
		                                                   "body_count",
		                                                   "shape_count",
		                                                   "query_count",
		                                                   "constraint_count",
		                                                   "scene_instance_count",
		                                                   "workload_elapsed_ms",
		                                                   "render_elapsed_ms",
		                                                   "present_wait_ms",
		                                                   "received_snapshot_count",
		                                                   "rendered_snapshot_count",
		                                                   "superseded_snapshot_count",
		                                                   "first_displayed_work_unit_index",
		                                                   "final_displayed_work_unit_index",
		                                                   "primary_metric_id",
		                                                   "primary_metric_value"};
		if (document.size() != keys.size())
			return ProofError(error, ArenaStatus_InvalidResult, "visual_proof_v4_key_count");
		std::uint32_t keyIndex = 0;
		for (nlohmann::ordered_json::const_iterator field = document.begin(); field != document.end();
		     ++field, ++keyIndex)
			if (field.key() != keys[keyIndex])
				return ProofError(error, ArenaStatus_InvalidResult, "visual_proof_v4_key_order");
		if (document.value("work_unit_id", "") != CatalogTextView(catalog, benchmarkCase.workUnitId) ||
		    document.value("body_count", 0U) != benchmarkCase.bodyCount ||
		    document.value("shape_count", 0U) != benchmarkCase.shapeCount ||
		    document.value("query_count", 0U) != benchmarkCase.queryCount ||
		    document.value("constraint_count", 0U) != benchmarkCase.constraintCount ||
		    document.value("scene_instance_count", 0U) != benchmarkCase.visualInstanceCount ||
		    document.value("primary_metric_id", "") != CatalogTextView(catalog, benchmarkCase.primaryMetricId) ||
		    (qualityOnly != 0
		         ? !document["present_wait_ms"].is_null() || !document["primary_metric_value"].is_null()
				 : !document["present_wait_ms"].is_number() || !document["primary_metric_value"].is_number()))
			return ProofError(error, ArenaStatus_InvalidResult, "visual_proof_v4_identity");
		if (qualityOnly == 0)
		{
			proof->presentWaitMs = document["present_wait_ms"].get<double>();
			proof->primaryMetricValue = document["primary_metric_value"].get<double>();
			const double expectedPrimary = CatalogTextView(catalog, benchmarkCase.workUnitId) == "query_batch"
			                                   ? static_cast<double>(benchmarkCase.queryCount) *
			                                         static_cast<double>(expectedSteps) * 1000.0 / proof->physicsTotalMs
			                                   : proof->physicsTotalMs / static_cast<double>(expectedSteps);
			if (!std::isfinite(proof->presentWaitMs) || proof->presentWaitMs < 0.0 ||
			    proof->presentWaitMs > proof->renderTotalMs || !std::isfinite(proof->primaryMetricValue) ||
			    std::abs(proof->primaryMetricValue - expectedPrimary) >
			        std::max(0.000001, std::abs(expectedPrimary) * 0.000000001))
				return ProofError(error, ArenaStatus_InvalidResult, "visual_proof_v4_values");
		}
		proof->receivedSnapshotCount = document.value("received_snapshot_count", 0U);
		proof->renderedSnapshotCount = document.value("rendered_snapshot_count", 0U);
		proof->supersededSnapshotCount = document.value("superseded_snapshot_count", UINT32_MAX);
		proof->firstDisplayedStepIndex = document.value("first_displayed_work_unit_index", 0U);
		proof->finalDisplayedStepIndex = document.value("final_displayed_work_unit_index", 0U);
		if (proof->receivedSnapshotCount != expectedSteps ||
		    proof->renderedSnapshotCount < (expectedSteps > 1 ? 2U : 1U) ||
		    proof->receivedSnapshotCount != proof->renderedSnapshotCount + proof->supersededSnapshotCount ||
		    proof->firstDisplayedStepIndex != 1 || proof->finalDisplayedStepIndex != expectedSteps)
			return ProofError(error, ArenaStatus_InvalidResult, "visual_proof_count_values");
	}
	catch (...)
	{
		return ProofError(error, ArenaStatus_InvalidResult, "visual_proof_json");
	}
	if (proof->completedStepCount != expectedSteps ||
	    (qualityOnly == 0 && (!std::isfinite(proof->physicsTotalMs) || proof->physicsTotalMs <= 0.0 ||
	                          !std::isfinite(proof->renderTotalMs) || proof->renderTotalMs < 0.0)))
		return ProofError(error, ArenaStatus_InvalidResult, "visual_proof_values");
	return ArenaStatus_Ok;
}
} // namespace physics_arena
