#include "launcher_app_internal.h"
#include "pyramid_wall.h"

#include <imgui.h>
#include <algorithm>
#include <array>
#include <cstdio>

namespace benchmark_visual
{
using namespace physics_arena;
namespace
{
int BeginFacts(const char* id)
{
	if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH))
		return 0;
	const float scale = ImGui::GetStyle().FontScaleDpi;
	ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, (std::min)(180 * scale, ImGui::GetContentRegionAvail().x * 0.4f));
	ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
	return 1;
}

void Fact(std::string_view label, std::string_view value)
{
	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	ImGui::TextWrapped("%.*s", static_cast<int>(label.size()), label.data());
	ImGui::TableNextColumn();
	ImGui::TextWrapped("%.*s", static_cast<int>(value.size()), value.data());
}

void CountFact(const char* label, std::uint64_t value)
{
	Fact(label, FormatNativeCount(value).data());
}

void QuantityFact(const char* label, double value, const char* unit = "")
{
	std::array<char, 640> text = {};
	std::snprintf(text.data(), text.size(), "%s %s", FormatNativeValue(value, SelectNativeValueFormat(NativeValueDomain_Quantity, 0)).data(), unit);
	Fact(label, text.data());
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s %s", FormatNativeRaw(value).data(), unit);
}

void AxisFact(const char* label, const NativeValueText* values, const char* const* axes, int count, const char* unit)
{
	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	ImGui::TextWrapped("%s", label);
	ImGui::TableNextColumn();
	ImGui::PushID(label);
	const float scale = ImGui::GetStyle().FontScaleDpi;
	const int columns = ImGui::GetContentRegionAvail().x < count * 100 * scale ? 1 : count;
	if (ImGui::BeginTable("components", columns, ImGuiTableFlags_SizingStretchSame))
	{
		for (int index = 0; index < count; ++index)
		{
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", axes[index]);
			ImGui::SameLine(0, 6 * scale);
			ImGui::Text("%s %s", values[index].data(), unit);
		}
		ImGui::EndTable();
	}
	ImGui::PopID();
}

void VectorFact(const char* label, CaseExecutionVector3 value, const char* unit = "m")
{
	const NativeValueFormat format = SelectNativeValueFormat(NativeValueDomain_Quantity, 0);
	const std::array<NativeValueText, 3> values = {FormatNativeValue(value.x, format), FormatNativeValue(value.y, format), FormatNativeValue(value.z, format)};
	const std::array<const char*, 3> axes = {"X", "Y", "Z"};
	AxisFact(label, values.data(), axes.data(), 3, unit);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("X %s\nY %s\nZ %s %s", FormatNativeRaw(value.x).data(), FormatNativeRaw(value.y).data(), FormatNativeRaw(value.z).data(), unit);
}

void GridFact(const char* label, const std::uint32_t* values, int count)
{
	std::array<NativeValueText, 3> text = {};
	for (int index = 0; index < count; ++index)
		text[index] = FormatNativeCount(values[index]);
	const std::array<const char*, 3> axes = {"X", count == 3 ? "Y" : "Z", "Z"};
	AxisFact(label, text.data(), axes.data(), count, "");
}

void BoxFacts(const CaseExecutionBox* boxes, std::uint32_t count)
{
	for (std::uint32_t index = 0; index < count; ++index)
	{
		ImGui::PushID(static_cast<int>(index));
		if (ImGui::TreeNode("static_box", "Static box %u", index))
		{
			if (BeginFacts("box_facts"))
			{
				VectorFact("Center", boxes[index].center);
				VectorFact("Half extents", boxes[index].halfExtents);
				ImGui::EndTable();
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}

void SavedSchedule(const ResultViewModel& model)
{
	if (!BeginFacts("saved_schedule"))
		return;
	CountFact("Bodies", model.bodyCount);
	CountFact("Shapes", model.shapeCount);
	CountFact("Queries", model.queryCount);
	CountFact("Constraints", model.constraintCount);
	const std::string_view workUnit = ResultViewTextView(&model, model.workUnitId);
	CountFact(workUnit == "ray_frame" ? "Warmup frames" : workUnit == "query_batch" ? "Warmup batches" : "Warmup steps", model.warmupWorkUnitCount);
	CountFact(workUnit == "ray_frame" ? "Measured frames" : workUnit == "query_batch" ? "Measured batches" : "Measured steps", model.measuredWorkUnitCount);
	if (workUnit == "ray_frame")
		Fact("Schedule", "Ray frames, no simulation timestep");
	else if (workUnit == "query_batch")
		Fact("Schedule", "Query batches");
	else
		QuantityFact("Frequency", model.timestepHz, "Hz");
	ImGui::EndTable();
}
}

void DrawCaseInformation(const EffectiveRunConfiguration& configuration)
{
	if (configuration.execution.bodyCount == 0)
		return;
	const CaseRecord& benchmarkCase = configuration.benchmarkCase;
	const std::string_view name = RunConfigurationTextView(&configuration, benchmarkCase.displayName);
	const std::string_view workUnit = RunConfigurationTextView(&configuration, benchmarkCase.workUnitLabel);
	const std::string_view primaryMetric = RunConfigurationTextView(&configuration, benchmarkCase.primaryMetricLabel);
	ImGui::SeparatorText("Case details");
	ImGui::TextWrapped("%.*s", static_cast<int>(name.size()), name.data());
	if (ImGui::BeginTable("run_case_facts", 2, ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Fact", ImGuiTableColumnFlags_WidthFixed, (std::min)(180 * ImGui::GetStyle().FontScaleDpi, ImGui::GetContentRegionAvail().x * 0.4f));
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextDisabled("Total bodies");
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(FormatNativeCount(benchmarkCase.bodyCount).data());
		if (benchmarkCase.dynamicBodyCount != 0)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("Dynamic bodies");
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(FormatNativeCount(benchmarkCase.dynamicBodyCount).data());
		}
		if (benchmarkCase.kinematicBodyCount != 0)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("Kinematic bodies");
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(FormatNativeCount(benchmarkCase.kinematicBodyCount).data());
		}
		if (benchmarkCase.staticBodyCount != 0)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("Static bodies");
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(FormatNativeCount(benchmarkCase.staticBodyCount).data());
		}
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextDisabled("Shapes");
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(FormatNativeCount(benchmarkCase.shapeCount).data());
		if (benchmarkCase.meshTriangleCount != 0)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("Mesh triangles");
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(FormatNativeCount(benchmarkCase.meshTriangleCount).data());
		}
		if (benchmarkCase.queryCount != 0)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("Queries");
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(FormatNativeCount(benchmarkCase.queryCount).data());
		}
		if (benchmarkCase.constraintCount != 0)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("Constraints");
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(FormatNativeCount(benchmarkCase.constraintCount).data());
		}
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextDisabled(benchmarkCase.timestepHz != 0 ? "Timestep" : "Work unit");
		ImGui::TableNextColumn();
		if (benchmarkCase.timestepHz != 0)
			ImGui::Text("%s Hz", FormatNativeCount(benchmarkCase.timestepHz).data());
		else
			ImGui::TextUnformatted(benchmarkCase.fixtureKind == CaseFixtureKind_RayTracing ? "Ray frames" : "Query batches");
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextDisabled("Warmup");
		ImGui::TableNextColumn();
		ImGui::Text("%s %.*s%s", FormatNativeCount(benchmarkCase.warmupWorkUnitCount).data(), static_cast<int>(workUnit.size()), workUnit.data(),
		            benchmarkCase.warmupWorkUnitCount == 1 ? "" : "s");
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextDisabled("Measured");
		ImGui::TableNextColumn();
		ImGui::Text("%s %.*s%s", FormatNativeCount(benchmarkCase.measuredWorkUnitCount).data(), static_cast<int>(workUnit.size()),
		            workUnit.data(), benchmarkCase.measuredWorkUnitCount == 1 ? "" : "s");
		ImGui::EndTable();
	}
	ImGui::TextDisabled("Primary metric");
	ImGui::TextWrapped("%.*s", static_cast<int>(primaryMetric.size()), primaryMetric.data());
	if (benchmarkCase.resultGroupCount != 0)
	{
		ImGui::TextDisabled("Result groups");
		for (std::uint32_t group = 0; group < benchmarkCase.resultGroupCount; ++group)
		{
			const std::string_view label =
			    RunConfigurationTextView(&configuration, configuration.resultGroups[group].label);
			ImGui::TextWrapped("%.*s", static_cast<int>(label.size()), label.data());
		}
	}
}

void DrawScenario(const CaseExecutionSpec& spec)
{
	constexpr std::array<const char*, 5> shapes = {"unspecified", "boxes", "spheres", "capsules", "convex hulls"};
	const char* shape = shapes[spec.selectedGeometry.shape];
	switch (spec.fixtureKind)
	{
	case CaseFixtureKind_OpenContainerFallingPile:
		ImGui::TextWrapped("A falling pile of %u %s starts at height %.3g m inside a container made from %u static boxes. Gravity brings the pile into contact with the floor, walls and other bodies.",
		                   spec.dynamicBodyCount, shape, spec.openContainer.dynamicInitialY, spec.openContainer.staticBoxCount);
		break;
	case CaseFixtureKind_BoxContactIslands:
		ImGui::TextWrapped("%u x %u separated islands each contain a %u x %u x %u grid of %s above a floor. The separation creates independent contact workloads.",
		                   spec.contactIslands.islandGrid[0], spec.contactIslands.islandGrid[1],
		                   spec.contactIslands.bodyGrid[0], spec.contactIslands.bodyGrid[1], spec.contactIslands.bodyGrid[2], shape);
		break;
	case CaseFixtureKind_SpatialQueryTrace:
		ImGui::TextWrapped("Each query batch traces %u rays, %u sphere casts and %u overlaps against %u static bodies. Hit observations describe the query results. A batch is a unit of query work, not a simulated timestep.",
		                   spec.spatialQuery.rayCount, spec.spatialQuery.sphereCastCount, spec.spatialQuery.overlapCount, spec.staticBodyCount);
		break;
	case CaseFixtureKind_RagdollStairTumble:
		ImGui::TextWrapped("A %u x %u grid of articulated ragdolls tumbles over %u stairs. Each ragdoll contains %u parts and %u links. Trigger and follower rows start with speeds %.3g and %.3g m/s.",
		                   spec.ragdoll.ragdollGrid[0], spec.ragdoll.ragdollGrid[1], spec.ragdoll.stairCount,
		                   spec.ragdoll.partCount, spec.ragdoll.linkCount, spec.ragdoll.triggerRowSpeed, spec.ragdoll.followerRowSpeed);
		break;
	case CaseFixtureKind_PyramidWall:
		ImGui::TextWrapped("A triangular wall of %u stacked cubes in %u rows, one cube deep. %u warmup steps and %u measured steps share the same world. Energy, support penetration and lateral displacement describe its physical behaviour separately from execution cost.",
		    spec.dynamicBodyCount, spec.pyramidWall.rowCount, spec.warmupWorkUnitCount, spec.measuredWorkUnitCount);
		break;
	case CaseFixtureKind_RayTracing:
		ImGui::TextWrapped("%u views at %u x %u trace %u primitives and %u meshes containing %u triangles. Native closest, any-hit, filtered and enumeration queries are checked against shared double-precision intersections. An explicit %u-body pose update measures query acceleration-structure publication without simulation. The headline includes only ordinary coherent primary rays. Phase measurements and capability probes appear in Case data. Saved images open in Replay.",
		    spec.rayTracing.viewCount, spec.rayTracing.width, spec.rayTracing.height, spec.rayTracing.primitiveCount,
		    spec.rayTracing.meshCount, spec.meshTriangleCount, spec.rayTracing.movingCount);
		break;
	case CaseFixtureKind_LargePyramid:
		ImGui::TextWrapped("A %u-row pyramid of %s rests on a floor before %u projectiles launch after %u completed steps (%.3f s). Launch velocity is (%.3g, %.3g, %.3g) m/s.",
		                   spec.largePyramid.rowCount, shape, spec.largePyramid.projectileCount,
		                   spec.largePyramid.projectileLaunchAfterWorkUnits,
		                   static_cast<double>(spec.largePyramid.projectileLaunchAfterWorkUnits) / spec.timestepHz,
		                   spec.largePyramid.projectileLaunchVelocity.x, spec.largePyramid.projectileLaunchVelocity.y,
		                   spec.largePyramid.projectileLaunchVelocity.z);
		break;
	default:
		break;
	}
}

void DrawMeasurementMeaning(ResultMeasurementMode mode, std::string_view workUnit)
{
	if (mode == ResultMeasurementMode_PhysicalQuality)
		ImGui::TextWrapped("Physical quality only. Joint coverage and state observations are recorded without timing or a speed ranking. Joint gaps are descriptive: no physical gap tolerance is defined. Successful execution and finite transforms do not establish physical acceptance.");
	else if (workUnit == "ray_frame")
		ImGui::TextWrapped("The primary frame measures native ordinary coherent closest rays through worker completion and result mapping. Setup, conditioning, correctness checks and recording are separate. Phase throughput uses each phase's actual query count. Native batch results are labelled separately; they do not imply SIMD execution.");
	else if (workUnit == "query_batch")
		ImGui::TextWrapped("Performance measures complete query batches. Throughput counts queries per second. The repeat value is derived from its saved workload duration, not a median of individual query latencies.");
	else
		ImGui::TextWrapped("Performance measures the complete native step, including internal substeps and worker completion. Setup, reset, warmup, rendering and file output are outside that timed region. A repeat reports its mean per work unit. Summary minimum, median and maximum compare those repeat measurements. Step timing describes its explicitly selected step population.");
}

const char* CasePurpose(CaseFixtureKind kind)
{
	switch (kind)
	{
	case CaseFixtureKind_OpenContainerFallingPile:
		return "Measures native step cost for a falling pile and dense contacts in a container";
	case CaseFixtureKind_BoxContactIslands:
		return "Exercises separated contact workloads and worker scaling";
	case CaseFixtureKind_SpatialQueryTrace:
		return "Measures ray, sphere-cast and overlap query batches against static geometry";
	case CaseFixtureKind_RagdollStairTumble:
		return "Observes articulated joints and body state while ragdolls tumble down stairs";
	case CaseFixtureKind_PyramidWall:
		return "Measures support and shape of a tall triangular cube wall, with a fixed 0.10 m (10 cm) allowance throughout its unforced run";
	case CaseFixtureKind_RayTracing:
		return "Measures native ray query throughput, surface correctness and scene-update cost";
	case CaseFixtureKind_LargePyramid:
		return "Exercises pyramid support followed by the configured projectile impact";
	default:
		return "";
	}
}

void DrawRunSummaryFact(const char* label, const char* value)
{
	ImGui::TableNextColumn();
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Body), 10 * kNativeUiBodyLineScale);
	ImGui::PushTextWrapPos(0);
	ImGui::TextDisabled("%s", label);
	ImGui::PopTextWrapPos();
	ImGui::PopFont();
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_DataStrong), 12);
	ImGui::TextUnformatted(value);
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		ImGui::SetTooltip("%s", value);
	ImGui::PopFont();
}

void DrawRunCaseSummary(const EffectiveRunConfiguration& configuration)
{
	const CaseExecutionSpec& spec = configuration.execution;
	const CaseRecord& record = configuration.benchmarkCase;
	const std::string_view work = RunConfigurationTextView(&configuration, record.workUnitId);
	const std::string_view name = RunConfigurationTextView(&configuration, record.displayName);
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Heading), 13 * kNativeUiBodyLineScale);
	ImGui::TextWrapped("%.*s", static_cast<int>(name.size()), name.data());
	ImGui::PopFont();
	const float scale = ImGui::GetStyle().FontScaleDpi;
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(7 * scale, 4 * scale));
	const int columns = ImGui::GetContentRegionAvail().x / scale < 600 ? 3 : 6;
	if (ImGui::BeginTable("case_fact_strip", columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX))
	{
		const std::array<const char*, 6> labels = {"Total bodies", work == "ray_frame" ? "Kinematic" : "Dynamic", "Static", "Shapes",
		    work == "ray_frame" ? "Primary rays/frame" : work == "query_batch" ? "Queries" : "Timestep",
		    work == "ray_frame" ? "Measured ray frames" : work == "query_batch" ? "Measured batches" : "Measured steps"};
		const std::array<std::uint32_t, 6> values = {record.bodyCount,
		    work == "ray_frame" ? record.kinematicBodyCount : record.dynamicBodyCount, record.staticBodyCount,
		    record.shapeCount, spec.timestepPresent != 0 ? spec.timestepHz : record.queryCount,
		    spec.measuredWorkUnitCount};
		for (std::size_t index = 0; index < labels.size(); ++index)
		{
			NativeValueText value = {};
			std::snprintf(value.data(), value.size(), "%s%s", FormatNativeCount(values[index]).data(),
			    index == 4 && spec.timestepPresent != 0 ? " Hz" : "");
			DrawRunSummaryFact(labels[index], value.data());
		}
		ImGui::EndTable();
	}
	ImGui::PopStyleVar();
}

void DrawCaseQualityLimits(const CaseExecutionSpec& spec)
{
	ImGui::SeparatorText("Expected behavior and limits");
	if (spec.fixtureKind == CaseFixtureKind_OpenContainerFallingPile)
		ImGui::TextWrapped("Falling and contact response are expected. Quality checks whole-box upward escape after entry and finite body states. Rebound counts and settling are not acceptance criteria");
	else if (spec.fixtureKind == CaseFixtureKind_BoxContactIslands)
		ImGui::TextWrapped("Authored boxes must remain finite, supported and within their assigned occupied shape during the final measured second. The shape allowance is 10%% of the minimum full box edge; movement alone does not fail");
	else if (spec.fixtureKind == CaseFixtureKind_LargePyramid)
	{
		ImGui::TextWrapped("Shape qualification uses a fixed 0.10 m (10 cm) allowance before the first possible-impact interval");
		ImGui::TextWrapped("The support phase is followed by the configured projectile event. Reported timings do not establish support stability or an acceptable disturbance response.");
	}
	else if (spec.fixtureKind == CaseFixtureKind_PyramidWall)
		ImGui::TextWrapped("Shape qualification uses a fixed 0.10 m (10 cm) allowance throughout the full unforced run, including construction and warmup");
	else if (spec.fixtureKind == CaseFixtureKind_SpatialQueryTrace)
		ImGui::TextWrapped("Declared expected hit counts apply to their matching query fixture and observation. Missing observations remain unknown and are never treated as measured zero.");
}

void DrawObservationDeclaration(const Catalog* catalog, const EffectiveRunConfiguration* configuration,
                                const ObservationDeclaration& declaration, std::uint32_t index,
                                std::string_view name, std::string_view unit, std::string_view work)
{
	ImGui::PushID(static_cast<int>(index));
	const int expanded = ImGui::TreeNode("declaration", "%.*s", static_cast<int>(name.size()), name.data());
	const char* role = declaration.role == ObservationRole_ValidityZero || declaration.role == ObservationRole_ValidityExact ? "Validation" :
	    declaration.role == ObservationRole_Performance ? "Performance" : "Descriptive";
	ImGui::TextDisabled("%s | %.*s | %u sample%s", role, static_cast<int>(unit.size()), unit.data(), declaration.sampleIndexCount, declaration.sampleIndexCount == 1 ? "" : "s");
	if (declaration.expectedValuePresence == PresenceStatus_Present)
		ImGui::Text("Expected per sample: %s", declaration.valueType == ObservationValueType_Uint64 ?
		    FormatNativeCount(declaration.expectedUnsigned).data() : FormatNativeRaw(declaration.expectedFloat64).data());
	if (expanded != 0)
	{
		if (BeginFacts("schedule"))
		{
			Fact("Phase", CaseConfigurationTextView(catalog, configuration, declaration.phaseId));
			std::array<std::uint32_t, kObservationSamplePerDeclarationCapacity> samples = {};
			for (std::uint32_t sample = 0; sample < declaration.sampleIndexCount; ++sample)
				samples[sample] = CaseConfigurationSampleIndex(catalog, configuration, declaration, sample);
			Fact(work == "ray_frame" ? "Final-sample indices" : work == "query_batch" ? "Query-batch indices" : "Completed-step indices",
			    FormatNativeThreadSet(samples.data(), declaration.sampleIndexCount).data());
			if (declaration.expectedValuePresence != PresenceStatus_Present)
				Fact("Expected value", "Not declared");
			ImGui::EndTable();
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void DrawRunCaseExplanation(const EffectiveRunConfiguration& configuration)
{
	const CaseExecutionSpec& spec = configuration.execution;
	ImGui::TextWrapped("%s", CasePurpose(spec.fixtureKind));
	DrawCaseInformation(configuration);
	const std::string_view work = RunConfigurationTextView(&configuration, configuration.benchmarkCase.workUnitId);
	DrawMeasurementMeaning(spec.fixtureKind == CaseFixtureKind_RagdollStairTumble ? ResultMeasurementMode_PhysicalQuality : ResultMeasurementMode_Timed, work);
	DrawCaseQualityLimits(spec);
	if (spec.fixtureKind == CaseFixtureKind_OpenContainerFallingPile || spec.fixtureKind == CaseFixtureKind_RagdollStairTumble)
		ImGui::TextWrapped("Measurement uses a fresh fixture after the warmup world is destroyed");
	else if (spec.fixtureKind == CaseFixtureKind_SpatialQueryTrace)
		ImGui::TextWrapped("Measurement retains the warmed static world and query inputs, with measurement totals reset");
	else if (spec.fixtureKind == CaseFixtureKind_RayTracing)
		ImGui::TextWrapped("Measured ray suites follow warmup across the prescribed views. Aggregate observations use the final sample index");
	else
		ImGui::TextWrapped("Measurement continues in the warmed world, with the measured-step counter reset");
	ImGui::SeparatorText("Observation declarations");
	for (std::uint32_t group = 0; group < configuration.benchmarkCase.resultGroupCount; ++group)
	{
		const std::string_view groupName = RunConfigurationTextView(&configuration, configuration.resultGroups[group].label);
		ImGui::TextWrapped("%.*s", static_cast<int>(groupName.size()), groupName.data());
		for (std::uint32_t index = 0; index < configuration.benchmarkCase.observationCount; ++index)
		{
			const ObservationDeclaration& declaration = configuration.observations[index];
			if (declaration.resultGroupOrdinal == group)
				DrawObservationDeclaration(nullptr, &configuration, declaration, index,
				    RunConfigurationTextView(&configuration, declaration.label),
				    RunConfigurationTextView(&configuration, declaration.unit), work);
		}
	}
	if (configuration.benchmarkCase.observationCount == 0)
		ImGui::TextDisabled("No observation declarations available");
}

void DrawSavedCaseSummary(PhysicsArenaApp* app)
{
	const ResultViewModel& model = app->workspace.finalization.model;
	const EffectiveRunConfiguration* configuration = ResultViewConfiguration(&model);
	const CaseFixtureKind kind = configuration != nullptr ? configuration->execution.fixtureKind :
	    ResultViewCaseDefinition(&app->model.catalog, &model).fixtureKind;
	ImGui::TextWrapped("%s", CasePurpose(kind));
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 10);
	ImGui::PushTextWrapPos(0);
	ImGui::TextDisabled("%s | %s | %u engine%s, %u repeat%s", configuration != nullptr ? "Saved with this run" :
	    "Current catalog context; historical settings not saved", model.measurementMode == ResultMeasurementMode_PhysicalQuality ?
	    "Quality only, no timing or gap tolerance" : (ResultViewTextView(&model, model.workUnitId) == "ray_frame" ? "Native ray query time" :
	    ResultViewTextView(&model, model.workUnitId) == "query_batch" ? "Query batches" : "Complete native step time"),
	    model.engineCount, model.engineCount == 1 ? "" : "s", model.repeatCount, model.repeatCount == 1 ? "" : "s");
	ImGui::PopTextWrapPos();
	ImGui::PopFont();
}

void DrawSavedPyramidQuality(const ResultViewModel& model)
{
	if (model.requiredStabilityCriterion != StackCriterion_PyramidUnforcedShape)
		return;
	ImGui::SeparatorText("Saved pyramid quality");
	if (model.verificationMode == VerificationMode_Off)
	{
		ImGui::TextDisabled("Unverified: physical quality was not checked");
		return;
	}
	const char* window = ResultViewTextView(&model, model.caseId) == "large_pyramid_16206" ?
	    "Before the first possible-impact interval" : "Full unforced run";
	ImGui::TextWrapped("%s | Limit 0.10 m (10 cm)", window);
	if (model.stabilityResults.empty())
		ImGui::TextDisabled("Unassessed: saved trajectory evidence unavailable");
	for (const StackStabilityResult& result : model.stabilityResults)
	{
		const StackAssessment assessment = StackQualificationAssessment(result, model.requiredStabilityCriterion);
		const char* outcome = assessment == StackAssessment_Pass ? "Pass" : assessment == StackAssessment_Fail ? "Fail" : "Unassessed";
		ImGui::TextWrapped("%s | Threads %u | Repeat %u | %s", result.engineId.c_str(), result.threadCount, result.repeatIndex + 1, outcome);
		if (result.criterion != StackCriterion_PyramidUnforcedShape)
		{
			ImGui::TextDisabled("Historical fall-check outcome; current shape Unassessed");
			continue;
		}
		if (result.unforcedShapePresence == PresenceStatus_Present)
		{
			const StackMetric& metric = result.unforcedStackShapeExcess;
			const char* phase = metric.sample.phase == benchmark_stack::Phase_Construction ? "construction" :
			    metric.sample.phase == benchmark_stack::Phase_Warmup ? "warmup" : "measured";
			ImGui::TextWrapped("Shape excess %.4f m (%.2f cm) | Body %u | Segment %u, %s step %u",
			    metric.value, metric.value * 100, metric.body, metric.sample.segment, phase, metric.sample.step);
		}
		else
			ImGui::TextDisabled("Unforced shape evidence unavailable");
		const char* firstPhase = result.firstSample.phase == benchmark_stack::Phase_Construction ? "construction" :
		    result.firstSample.phase == benchmark_stack::Phase_Warmup ? "warmup" : "measured";
		const char* lastPhase = result.lastSample.phase == benchmark_stack::Phase_Construction ? "construction" :
		    result.lastSample.phase == benchmark_stack::Phase_Warmup ? "warmup" : "measured";
		ImGui::TextWrapped("Assessed segment %u, %s step %u to segment %u, %s step %u",
		    result.firstSample.segment, firstPhase, result.firstSample.step, result.lastSample.segment, lastPhase, result.lastSample.step);
		if (result.impactPresence == PresenceStatus_Present)
			ImGui::TextWrapped("Excluded possible-impact interval ending at segment %u, %s step %u",
			    result.impactIntervalEnd.segment, result.impactIntervalEnd.phase == benchmark_stack::Phase_Warmup ? "warmup" : "measured",
			    result.impactIntervalEnd.step);
	}
}

void DrawSavedCaseExplanation(PhysicsArenaApp* app)
{
	const ResultViewModel& model = app->workspace.finalization.model;
	const EffectiveRunConfiguration* configuration = ResultViewConfiguration(&model);
	const CaseFixtureKind kind = configuration != nullptr ? configuration->execution.fixtureKind :
	    ResultViewCaseDefinition(&app->model.catalog, &model).fixtureKind;
	ImGui::TextWrapped("%s", CasePurpose(kind));
	ImGui::TextDisabled(configuration != nullptr ? "Saved with this run" : "Historical settings: Not saved");
	if (configuration != nullptr)
		DrawCaseInformation(*configuration);
	else
	{
		ImGui::TextDisabled("Purpose and declarations: Current catalog context");
		SavedSchedule(model);
	}
	if (ImGui::TreeNode("Measurement and quality limits"))
	{
		DrawMeasurementMeaning(model.measurementMode, ResultViewTextView(&model, model.workUnitId));
		if (configuration != nullptr)
			DrawCaseQualityLimits(configuration->execution);
		ImGui::TextWrapped("Validation retains declared expected values. Descriptive quantities have no inferred acceptance threshold. Equal solver iteration numbers do not establish equivalent work or quality");
		ImGui::TreePop();
	}
	DrawSavedPyramidQuality(model);
	ImGui::SeparatorText("Observation declarations");
	for (std::uint32_t group = 0; group < model.resultGroupCount; ++group)
	{
		const std::string_view groupName = ResultViewTextView(&model, model.resultGroups[group].label);
		ImGui::TextWrapped("%.*s", static_cast<int>(groupName.size()), groupName.data());
		for (std::uint32_t index = 0; index < model.observationCount; ++index)
		{
			const ResultObservationView& observation = model.observationViews[index];
			if (observation.resultGroupOrdinal != group)
				continue;
			const std::string_view name = ResultViewTextView(&model, observation.label);
			const std::string_view unit = ResultViewTextView(&model, observation.unit);
			const CaseRecord& definition = ResultViewCaseDefinition(&app->model.catalog, &model);
			const ObservationDeclaration& declaration = CaseConfigurationObservation(&app->model.catalog, definition, configuration, index);
			DrawObservationDeclaration(&app->model.catalog, configuration, declaration, index, name, unit,
			    ResultViewTextView(&model, model.workUnitId));
		}
	}
	if (model.observationCount == 0)
		ImGui::TextDisabled("No observation declarations available");
}

void DrawFixtureFacts(const CaseExecutionSpec& spec)
{
	constexpr std::array<const char*, 5> shapes = {"Unspecified", "Box", "Sphere", "Capsule", "Convex hull"};
	constexpr std::array<const char*, 3> axes = {"Y", "X", "Z"};
	if (spec.fixtureKind != CaseFixtureKind_RayTracing && BeginFacts("selected_geometry"))
	{
		Fact("Shape", shapes[spec.selectedGeometry.shape]);
		if (spec.selectedGeometry.shape == CaseExecutionShape_Box || spec.selectedGeometry.shape == CaseExecutionShape_ConvexHull)
			VectorFact("Half extents", spec.selectedGeometry.halfExtents);
		if (spec.selectedGeometry.shape == CaseExecutionShape_Sphere || spec.selectedGeometry.shape == CaseExecutionShape_Capsule)
			QuantityFact("Radius", spec.selectedGeometry.radius, "m");
		if (spec.selectedGeometry.shape == CaseExecutionShape_Capsule)
		{
			QuantityFact("Half segment", spec.selectedGeometry.halfSegment, "m");
			Fact("Axis", axes[spec.selectedGeometry.axis]);
		}
		ImGui::EndTable();
	}
	if (spec.fixtureKind == CaseFixtureKind_OpenContainerFallingPile)
	{
		const CaseExecutionOpenContainer& fixture = spec.openContainer;
		ImGui::SeparatorText("Dynamic pile");
		if (BeginFacts("dynamic_pile"))
		{
			GridFact("Grid", fixture.dynamicGrid, 3);
			VectorFact("Half extents", fixture.dynamicHalfExtents);
			VectorFact("Spacing", fixture.dynamicSpacing);
			QuantityFact("Initial height", fixture.dynamicInitialY, "m");
			QuantityFact("Density", fixture.density, "kg/m3");
			CountFact("Static box count", fixture.staticBoxCount);
			ImGui::EndTable();
		}
		ImGui::SeparatorText("Static container");
		BoxFacts(fixture.staticBoxes, fixture.staticBoxCount);
	}
	if (spec.fixtureKind == CaseFixtureKind_BoxContactIslands && BeginFacts("contact_islands"))
	{
		const CaseExecutionContactIslands& fixture = spec.contactIslands;
		GridFact("Island grid", fixture.islandGrid, 2);
		QuantityFact("Island spacing X", fixture.islandSpacing[0], "m");
		QuantityFact("Island spacing Z", fixture.islandSpacing[1], "m");
		GridFact("Body grid", fixture.bodyGrid, 3);
		VectorFact("Body half extents", fixture.bodyHalfExtents);
		VectorFact("Body spacing", fixture.bodySpacing);
		QuantityFact("Initial height", fixture.bodyInitialY, "m");
		VectorFact("Floor half extents", fixture.floorHalfExtents);
		QuantityFact("Density", fixture.density, "kg/m3");
		ImGui::EndTable();
	}
	if (spec.fixtureKind == CaseFixtureKind_SpatialQueryTrace && BeginFacts("queries"))
	{
		const CaseExecutionSpatialQuery& fixture = spec.spatialQuery;
		GridFact("Static grid", fixture.staticGrid, 3);
		VectorFact("Static half extents", fixture.staticHalfExtents);
		VectorFact("Static spacing", fixture.staticSpacing);
		VectorFact("Static base center", fixture.staticBaseCenter);
		CountFact("Ray count", fixture.rayCount);
		CountFact("Sphere cast count", fixture.sphereCastCount);
		CountFact("Overlap count", fixture.overlapCount);
		QuantityFact("Query distance", fixture.queryDistance, "m");
		QuantityFact("Sphere cast radius", fixture.sphereCastRadius, "m");
		VectorFact("Overlap half extents", fixture.overlapHalfExtents);
		QuantityFact("Miss offset", fixture.missOffset, "m");
		CountFact("Debug samples per family", fixture.debugSamplesPerFamily);
		ImGui::EndTable();
	}
	if (spec.fixtureKind == CaseFixtureKind_PyramidWall && BeginFacts("pyramid_wall"))
	{
		const CaseExecutionPyramidWall& wall = spec.pyramidWall;
		CountFact("Rows", wall.rowCount);
		CountFact("Dynamic cubes", spec.dynamicBodyCount);
		QuantityFact("Cube edge", 2 * wall.halfExtent, "m");
		QuantityFact("Cube mass", 8.0 * wall.halfExtent * wall.halfExtent * wall.halfExtent * wall.density, "kg");
		QuantityFact("Initial centre of mass", wall.halfExtent * (2.0 * wall.rowCount + 1) / 3.0, "m");
		QuantityFact("Initial potential energy", PyramidWallInitialPotentialEnergy(wall, spec.gravity.y), "J");
		ImGui::EndTable();
	}
	if (spec.fixtureKind == CaseFixtureKind_RayTracing && BeginFacts("ray_tracing"))
	{
		const CaseExecutionRayTracing& fixture = spec.rayTracing;
		CountFact("Primitives", fixture.primitiveCount);
		CountFact("Static meshes", fixture.meshCount);
		CountFact("Triangles per mesh", fixture.trianglesPerMesh);
		CountFact("Triangles", spec.meshTriangleCount);
		CountFact("Kinematic bodies", fixture.movingCount);
		CountFact("Primary rays per frame", spec.queryCount);
		CountFact("Camera views", fixture.viewCount);
		CountFact("Warmup suites", spec.warmupWorkUnitCount);
		CountFact("Measured suites", spec.measuredWorkUnitCount);
		CountFact("Image width (px)", fixture.width);
		CountFact("Image height (px)", fixture.height);
		CountFact("Recipe revision", fixture.recipeRevision);
		ImGui::EndTable();
	}
	if (spec.fixtureKind == CaseFixtureKind_RagdollStairTumble)
	{
		const CaseExecutionRagdoll& fixture = spec.ragdoll;
		if (BeginFacts("ragdoll"))
		{
			GridFact("Ragdoll grid", fixture.ragdollGrid, 2);
			QuantityFact("Column spacing", fixture.columnSpacing, "m");
			QuantityFact("Row spacing", fixture.rowSpacing, "m");
			QuantityFact("Base height offset", fixture.baseHeightOffset, "m");
			QuantityFact("Pitch", fixture.pitchDegrees, "deg");
			CountFact("Yaw pattern count", fixture.yawPatternCount);
			for (std::uint32_t index = 0; index < fixture.yawPatternCount; ++index)
			{
				std::array<char, 48> label = {};
				std::snprintf(label.data(), label.size(), "Yaw pattern %u", index);
				QuantityFact(label.data(), fixture.yawPatternDegrees[index], "deg");
			}
			QuantityFact("Trigger row speed", fixture.triggerRowSpeed, "m/s");
			QuantityFact("Follower row speed", fixture.followerRowSpeed, "m/s");
			CountFact("Stair count", fixture.stairCount);
			QuantityFact("Stair rise", fixture.stairRise, "m");
			QuantityFact("Stair depth", fixture.stairDepth, "m");
			QuantityFact("Stair half width", fixture.stairHalfWidth, "m");
			QuantityFact("Stair half height", fixture.stairHalfHeight, "m");
			QuantityFact("Stair half depth", fixture.stairHalfDepth, "m");
			CountFact("Extra static box count", fixture.extraStaticBoxCount);
			CountFact("Part count", fixture.partCount);
			CountFact("Link count", fixture.linkCount);
					QuantityFact("Part mass", fixture.partMass, "kg");
			Fact("Linked collisions", fixture.linkedCollisionMode == CaseExecutionToggle_Enabled ? "On" : "Off");
			ImGui::EndTable();
		}
		BoxFacts(fixture.extraStaticBoxes, fixture.extraStaticBoxCount);
		for (std::uint32_t index = 0; index < fixture.partCount; ++index)
		{
			ImGui::PushID(static_cast<int>(index));
			if (ImGui::TreeNode("part", "Part %u", index))
			{
				if (BeginFacts("part_facts"))
				{
					const CaseExecutionRagdollPart& part = fixture.parts[index];
					Fact("Shape", shapes[part.shape]);
					VectorFact("Center", part.center);
					if (part.shape == CaseExecutionShape_Box || part.shape == CaseExecutionShape_ConvexHull)
						VectorFact("Half extents", part.halfExtents);
					if (part.shape == CaseExecutionShape_Sphere || part.shape == CaseExecutionShape_Capsule)
						QuantityFact("Radius", part.radius, "m");
					if (part.shape == CaseExecutionShape_Capsule)
					{
						QuantityFact("Half segment", part.halfSegment, "m");
						Fact("Axis", axes[part.axis]);
					}
					ImGui::EndTable();
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		for (std::uint32_t index = 0; index < fixture.linkCount; ++index)
		{
			ImGui::PushID(static_cast<int>(index));
			if (ImGui::TreeNode("link", "Link %u", index))
			{
				if (BeginFacts("link_facts"))
				{
					CountFact("Parent part", fixture.links[index].parentPart);
					CountFact("Child part", fixture.links[index].childPart);
					VectorFact("Anchor", fixture.links[index].anchor);
					VectorFact("Parent local anchor", fixture.links[index].parentLocalAnchor);
					VectorFact("Child local anchor", fixture.links[index].childLocalAnchor);
					ImGui::EndTable();
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
	}
	if (spec.fixtureKind == CaseFixtureKind_LargePyramid && BeginFacts("pyramid"))
	{
		const CaseExecutionLargePyramid& fixture = spec.largePyramid;
		CountFact("Row count", fixture.rowCount);
		VectorFact("Box half extents", fixture.boxHalfExtents);
		VectorFact("Box spacing", fixture.boxSpacing);
		VectorFact("Base center", fixture.baseCenter);
		VectorFact("Floor half extents", fixture.floorHalfExtents);
		QuantityFact("Box density", fixture.boxDensity, "kg/m3");
		CountFact("Projectile count", fixture.projectileCount);
		QuantityFact("Projectile radius", fixture.projectileRadius, "m");
		QuantityFact("Projectile density", fixture.projectileDensity, "kg/m3");
		VectorFact("Projectile initial center", fixture.projectileInitialCenter);
		VectorFact("Projectile center spacing", fixture.projectileCenterSpacing);
		VectorFact("Projectile launch velocity", fixture.projectileLaunchVelocity, "m/s");
		CountFact("Launch after completed steps", fixture.projectileLaunchAfterWorkUnits);
		ImGui::EndTable();
	}
}

void DrawSavedEngineSettings(const EngineRunSettings& profile, CaseFixtureKind fixture, std::string_view engineId, float scale)
{
	const CaseNativeSolver& solver = profile.solver;
	if (!CaseExecutionIsQuery(fixture) && BeginFacts("saved_engine_physics"))
	{
		QuantityFact("Friction", profile.friction);
		QuantityFact("Restitution", profile.restitution);
		Fact("Sleep", profile.sleepMode == CaseExecutionToggle_Enabled ? "On" : "Off");
		Fact("Continuous collision detection", profile.continuousCollisionMode == CaseExecutionToggle_Enabled ? "On" : "Off");
		if (fixture == CaseFixtureKind_RagdollStairTumble)
		{
			QuantityFact("Linear damping", profile.linearDamping);
			QuantityFact("Angular damping", profile.angularDamping);
		}
		if (engineId == "unity_physics")
			Fact("Contact solver stabilization", profile.solverStabilizationPresence == PresenceStatus_Absent ? "Unavailable" :
			    profile.solverStabilization == CaseExecutionToggle_Enabled ? "On" : "Off");
		ImGui::EndTable();
	}
	if (solver.supportedFields == 0)
		ImGui::TextDisabled("No structured solver controls");
	else if (ImGui::BeginTable("saved_solver", 2, ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100 * scale);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
		constexpr std::array<const char*, CaseSolverField_Count> labels = {"Velocity iterations", "Position iterations", "Projection iterations", "Solver iterations", "Substeps", "Collision steps"};
		for (std::uint32_t field = 0; field < CaseSolverField_Count; ++field)
			if ((solver.supportedFields & (1u << field)) != 0)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				const float baseline = ImGui::GetCursorPosY() + ImGui::GetFontBaked()->Ascent;
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				ImGui::TextWrapped("%s", labels[field]);
				ImGui::PopStyleColor();
				ImGui::TableNextColumn();
				ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 11);
				ImGui::SetCursorPosY(baseline - ImGui::GetFontBaked()->Ascent);
				ImGui::Text("%s", FormatNativeCount(solver.values[field]).data());
				ImGui::PopFont();
			}
		ImGui::EndTable();
	}
}

void DrawSavedEngineConfiguration(PhysicsArenaApp* app, std::uint32_t index)
{
	const EffectiveRunConfiguration* configuration = ResultViewConfiguration(&app->workspace.finalization.model);
	if (configuration == nullptr)
		ImGui::TextDisabled("Structured settings: Not saved");
	else
		DrawSavedEngineSettings(configuration->selectedEngineSettings[index], configuration->execution.fixtureKind,
		    ResultViewTextView(&app->workspace.finalization.model, app->workspace.finalization.model.engines[index].id), app->platform.dpiScale);
}

void DrawRecordedEngineDetails(PhysicsArenaApp* app, std::uint32_t index)
{
	const ResultViewModel& model = app->workspace.finalization.model;
	const ResultEngineView& engine = model.engines[index];
	const std::string_view settings = ResultViewTextView(&model, engine.physicsSettings);
	const std::string_view build = ResultViewTextView(&model, engine.buildSettings);
	ImGui::TextDisabled("Recorded at %u threads", engine.physicsSettingsThreadCount);
	ImGui::TextDisabled("Native settings");
	ImGui::BeginChild("native_settings_text", ImVec2(0, 48 * app->platform.dpiScale), ImGuiChildFlags_Borders,
	    ImGuiWindowFlags_HorizontalScrollbar);
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 11);
	ImGui::TextUnformatted(settings.data(), settings.data() + settings.size());
	ImGui::PopFont();
	ImGui::EndChild();
	ImGui::TextDisabled("Build");
	ImGui::BeginChild("build_text", ImVec2(0, 48 * app->platform.dpiScale), ImGuiChildFlags_Borders,
	    ImGuiWindowFlags_HorizontalScrollbar);
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 11);
	ImGui::TextUnformatted(build.data(), build.data() + build.size());
	ImGui::PopFont();
	ImGui::EndChild();
	if (ImGui::Button("Copy engine details"))
	{
		std::array<char, 2 * kCatalogTextValueCapacity + 96> text = {};
		std::snprintf(text.data(), text.size(), "Threads: %u\nNative settings: %.*s\nBuild: %.*s", engine.physicsSettingsThreadCount,
		    static_cast<int>(settings.size()), settings.data(), static_cast<int>(build.size()), build.data());
		ImGui::SetClipboardText(text.data());
	}
}

void DrawSavedCaseConfiguration(PhysicsArenaApp* app)
{
	const ResultViewModel& model = app->workspace.finalization.model;
	const EffectiveRunConfiguration* configuration = ResultViewConfiguration(&model);
	if (configuration == nullptr)
	{
		ImGui::TextWrapped("Historical settings: Not saved");
		SavedSchedule(model);
	}
	else
	{
		ImGui::TextDisabled("Saved with this run");
		DrawCaseInformation(*configuration);
		const CaseExecutionSpec& spec = configuration->execution;
		if (spec.fixtureKind != CaseFixtureKind_RayTracing)
		{
			ImGui::SeparatorText("Environment");
			if (BeginFacts("environment"))
			{
				VectorFact("Gravity", spec.gravity, "m/s2");
					ImGui::EndTable();
			}
		}
		ImGui::SeparatorText("Fixture");
		DrawFixtureFacts(spec);
	}
	ImGui::SeparatorText("Engine configuration and workers");
	for (std::uint32_t index = 0; index < model.engineCount; ++index)
	{
		const ResultEngineView& engine = model.engines[index];
		const std::string_view name = ResultViewTextView(&model, engine.provenanceLabel);
		std::array<char, kEngineProvenanceLabelCapacity> heading = {};
		std::snprintf(heading.data(), heading.size(), "%.*s", static_cast<int>(name.size()), name.data());
		if (!ImGui::TreeNode(heading.data()))
			continue;
		DrawSavedEngineConfiguration(app, index);
		DrawRecordedEngineDetails(app, index);
		ImGui::TreePop();
	}
}
}
