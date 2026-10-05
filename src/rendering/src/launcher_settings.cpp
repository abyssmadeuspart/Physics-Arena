#include "launcher_app_internal.h"

#include <imgui.h>
#include <algorithm>
#include <array>
#include <cstdio>

namespace benchmark_visual
{
namespace
{
using namespace physics_arena;

int BeginSettingGroup(const char* name, std::string_view widestLabel, PresenceStatus units = PresenceStatus_Present)
{
	const float scale = ImGui::GetStyle().FontScaleDpi;
	const float labelWidth = (std::min)({ImGui::CalcTextSize(widestLabel.data(), widestLabel.data() + widestLabel.size()).x + 7 * scale, 180 * scale, ImGui::GetContentRegionAvail().x * 0.40f});
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7 * scale, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(7 * scale, 3.5f * scale));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5 * scale, 5.5f * scale));
	if (!ImGui::BeginTable(name, units == PresenceStatus_Present ? 3 : 2,
	    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
	{
		ImGui::PopStyleVar(3);
		return 0;
	}
	ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
	ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
	if (units == PresenceStatus_Present)
		ImGui::TableSetupColumn("Unit", ImGuiTableColumnFlags_WidthFixed, 35 * scale);
	return 1;
}

void EndSettingGroup()
{
	ImGui::EndTable();
	ImGui::PopStyleVar(3);
}

void SettingHelp(const char* help)
{
	if (help != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(300 * ImGui::GetStyle().FontScaleDpi);
		ImGui::TextUnformatted(help);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

int BeginSettingRow(const char* label, const char* unit, const char* help = nullptr)
{
	const float scale = ImGui::GetStyle().FontScaleDpi;
	ImGui::PushID(label);
	ImGui::TableNextRow(0, 30 * scale);
	ImGui::TableSetColumnIndex(0);
	ImGui::AlignTextToFramePadding();
	ImGui::PushTextWrapPos(0);
	ImGui::TextUnformatted(label);
	ImGui::PopTextWrapPos();
	SettingHelp(help);
	if (ImGui::TableGetColumnCount() == 3)
	{
		ImGui::TableSetColumnIndex(2);
		ImGui::AlignTextToFramePadding();
		ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 10);
		ImGui::TextDisabled("%s", unit);
		ImGui::PopFont();
	}
	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-1);
	ImGui::PushFont(NativeUiFontFace(NativeUiFont_Data), 11);
	return 1;
}

void EndSettingRow()
{
	ImGui::PopFont();
	ImGui::PopID();
}

int VectorComponents(void* values, ImGuiDataType type, std::size_t stride, int count, const char* axes)
{
	int changed = 0;
	const float scale = ImGui::GetStyle().FontScaleDpi;
	const float width = (ImGui::GetContentRegionAvail().x - (count - 1) * 5 * scale) / count;
	for (int index = 0; index < count; ++index)
	{
		ImGui::PushID(index);
		if (index != 0)
			ImGui::SameLine(0, 5 * scale);
		ImGui::BeginGroup();
		ImGui::TextDisabled("%c", axes[index]);
		ImGui::SetNextItemWidth(width);
		changed |= ImGui::InputScalar("##component", type, static_cast<char*>(values) + index * stride);
		ImGui::EndGroup();
		ImGui::PopID();
	}
	return changed;
}

void UnsignedInput(const char* label, std::uint32_t* value, const char* unit = "", const char* help = nullptr)
{
	if (BeginSettingRow(label, unit, help) != 0)
	{
		ImGui::InputScalar("##value", ImGuiDataType_U32, value);
		SettingHelp(help);
		EndSettingRow();
	}
}

void CountInput(const char* label, std::uint16_t* value)
{
	if (BeginSettingRow(label, "") != 0)
	{
		ImGui::InputScalar("##value", ImGuiDataType_U16, value);
		EndSettingRow();
	}
}

void FloatInput(const char* label, float* value, const char* unit = "", const char* help = nullptr)
{
	if (BeginSettingRow(label, unit, help) != 0)
	{
		ImGui::InputFloat("##value", value);
		SettingHelp(help);
		EndSettingRow();
	}
}

void PairInput(const char* label, float* values, const char* unit = "m")
{
	if (BeginSettingRow(label, unit) != 0)
	{
		VectorComponents(values, ImGuiDataType_Float, sizeof(float), 2, "XZ");
		EndSettingRow();
	}
}

void GridInput(const char* label, std::uint32_t* values, int count)
{
	if (BeginSettingRow(label, "") != 0)
	{
		VectorComponents(values, ImGuiDataType_U32, sizeof(std::uint32_t), count, count == 2 ? "XZ" : "XYZ");
		EndSettingRow();
	}
}

void VectorInput(const char* label, CaseExecutionVector3* value, const char* unit = "m")
{
	if (BeginSettingRow(label, unit) != 0)
	{
		float components[3] = {value->x, value->y, value->z};
		if (VectorComponents(components, ImGuiDataType_Float, sizeof(float), 3, "XYZ"))
			*value = {components[0], components[1], components[2]};
		EndSettingRow();
	}
}

void SettingStatus(const char* label, const char* status, const char* help)
{
	BeginSettingRow(label, "", help);
	ImGui::AlignTextToFramePadding();
	ImGui::PushTextWrapPos(0);
	ImGui::TextUnformatted(status);
	ImGui::PopTextWrapPos();
	SettingHelp(help);
	EndSettingRow();
}

void ToggleInput(const char* label, CaseExecutionToggle* value, const char* help = nullptr)
{
	if (BeginSettingRow(label, "", help) != 0)
	{
		unsigned int enabled = *value == CaseExecutionToggle_Enabled ? 1 : 0;
		if (ImGui::CheckboxFlags("Enabled", &enabled, 1))
			*value = enabled != 0 ? CaseExecutionToggle_Enabled : CaseExecutionToggle_Disabled;
		SettingHelp(help);
		EndSettingRow();
	}
}

int ChoiceInput(const char* label, int* value, const char* choices)
{
	int changed = 0;
	if (BeginSettingRow(label, "") != 0)
	{
		changed = ImGui::Combo("##value", value, choices);
		EndSettingRow();
	}
	return changed;
}

void BoxInputs(CaseExecutionBox* boxes, std::uint16_t* count, std::uint32_t capacity)
{
	DrawPaneHeading("Static container", 28);
	if (BeginSettingGroup("box_count", "Static box count", PresenceStatus_Absent))
	{
		CountInput("Static box count", count);
		EndSettingGroup();
	}
	for (std::uint32_t index = 0; index < (std::min)(static_cast<std::uint32_t>(*count), capacity); ++index)
	{
		ImGui::PushID(static_cast<int>(index));
		if (ImGui::TreeNode("Static box", "Static box %u", index))
		{
			if (BeginSettingGroup("box_properties", "Half extents"))
			{
				VectorInput("Center", &boxes[index].center);
				VectorInput("Half extents", &boxes[index].halfExtents);
				EndSettingGroup();
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}

void FixtureInputs(CaseExecutionSpec* spec)
{
	if (spec->fixtureKind == CaseFixtureKind_OpenContainerFallingPile)
	{
		CaseExecutionOpenContainer& fixture = spec->openContainer;
		DrawPaneHeading("Dynamic pile", 28);
		if (BeginSettingGroup("dynamic_pile", "Initial height"))
		{
			GridInput("Grid", fixture.dynamicGrid, 3);
			VectorInput("Half extents", &fixture.dynamicHalfExtents);
			VectorInput("Spacing", &fixture.dynamicSpacing);
			FloatInput("Initial height", &fixture.dynamicInitialY, "m");
			FloatInput("Density", &fixture.density, "kg/m3");
			EndSettingGroup();
		}
		BoxInputs(fixture.staticBoxes, &fixture.staticBoxCount, kCaseExecutionStaticBoxCapacity);
	}
	else if (spec->fixtureKind == CaseFixtureKind_BoxContactIslands)
	{
		CaseExecutionContactIslands& fixture = spec->contactIslands;
		DrawPaneHeading("Contact islands", 28);
		if (!BeginSettingGroup("contact_islands", "Floor half extents"))
			return;
		GridInput("Island grid", fixture.islandGrid, 2);
		PairInput("Island spacing", fixture.islandSpacing);
		GridInput("Body grid", fixture.bodyGrid, 3);
		VectorInput("Body half extents", &fixture.bodyHalfExtents);
		VectorInput("Body spacing", &fixture.bodySpacing);
		FloatInput("Initial height", &fixture.bodyInitialY, "m");
		VectorInput("Floor half extents", &fixture.floorHalfExtents);
		FloatInput("Density", &fixture.density, "kg/m3");
		EndSettingGroup();
	}
	else if (spec->fixtureKind == CaseFixtureKind_SpatialQueryTrace)
	{
		CaseExecutionSpatialQuery& fixture = spec->spatialQuery;
		DrawPaneHeading("Static geometry and queries", 28);
		if (!BeginSettingGroup("query_fixture", "Debug samples per family"))
			return;
		GridInput("Static grid", fixture.staticGrid, 3);
		VectorInput("Static half extents", &fixture.staticHalfExtents);
		VectorInput("Static spacing", &fixture.staticSpacing);
		VectorInput("Static base center", &fixture.staticBaseCenter);
		UnsignedInput("Ray count", &fixture.rayCount);
		UnsignedInput("Sphere cast count", &fixture.sphereCastCount);
		UnsignedInput("Overlap count", &fixture.overlapCount);
		FloatInput("Query distance", &fixture.queryDistance, "m");
		FloatInput("Sphere cast radius", &fixture.sphereCastRadius, "m");
		VectorInput("Overlap half extents", &fixture.overlapHalfExtents);
		FloatInput("Miss offset", &fixture.missOffset, "m");
		UnsignedInput("Debug samples per family", &fixture.debugSamplesPerFamily);
		EndSettingGroup();
	}
	else if (spec->fixtureKind == CaseFixtureKind_RagdollStairTumble)
	{
		CaseExecutionRagdoll& fixture = spec->ragdoll;
		DrawPaneHeading("Ragdolls and stairs", 28);
		if (!BeginSettingGroup("ragdoll_fixture", "Base height offset"))
			return;
		GridInput("Ragdoll grid", fixture.ragdollGrid, 2);
		FloatInput("Column spacing", &fixture.columnSpacing, "m");
		FloatInput("Row spacing", &fixture.rowSpacing, "m");
		FloatInput("Base height offset", &fixture.baseHeightOffset, "m");
		FloatInput("Pitch degrees", &fixture.pitchDegrees, "deg");
		FloatInput("Trigger row speed", &fixture.triggerRowSpeed, "m/s");
		FloatInput("Follower row speed", &fixture.followerRowSpeed, "m/s");
		UnsignedInput("Stair count", &fixture.stairCount);
		FloatInput("Stair rise", &fixture.stairRise, "m");
		FloatInput("Stair depth", &fixture.stairDepth, "m");
		FloatInput("Stair half width", &fixture.stairHalfWidth, "m");
		FloatInput("Stair half height", &fixture.stairHalfHeight, "m");
		FloatInput("Stair half depth", &fixture.stairHalfDepth, "m");
		FloatInput("Part mass", &fixture.partMass, "kg");
		ToggleInput("Linked collisions", &fixture.linkedCollisionMode);
		CountInput("Yaw pattern count", &fixture.yawPatternCount);
		for (std::uint32_t index = 0;
		     index < (std::min)(static_cast<std::uint32_t>(fixture.yawPatternCount), kCaseExecutionYawCapacity);
		     ++index)
		{
			ImGui::PushID(static_cast<int>(index));
			FloatInput("Yaw degrees", &fixture.yawPatternDegrees[index], "deg");
			ImGui::PopID();
		}
		EndSettingGroup();
		BoxInputs(fixture.extraStaticBoxes, &fixture.extraStaticBoxCount, kCaseExecutionStaticBoxCapacity);
		if (BeginSettingGroup("part_count", "Part count", PresenceStatus_Absent))
		{
			CountInput("Part count", &fixture.partCount);
			EndSettingGroup();
		}
		for (std::uint32_t index = 0;
		     index < (std::min)(static_cast<std::uint32_t>(fixture.partCount), kCaseExecutionRagdollPartCapacity);
		     ++index)
		{
			ImGui::PushID(static_cast<int>(index));
			if (ImGui::TreeNode("Part", "Part %u", index))
			{
				if (BeginSettingGroup("part_properties", "Authored shape"))
				{
					CaseExecutionRagdollPart& part = fixture.parts[index];
					VectorInput("Center", &part.center);
					int shape = static_cast<int>(part.shape) - 1;
					if (ChoiceInput("Authored shape", &shape, "Box\0Sphere\0Capsule\0Convex Hull\0"))
						part.shape = static_cast<CaseExecutionShape>(shape + 1);
					VectorInput("Half extents", &part.halfExtents);
					FloatInput("Radius", &part.radius, "m");
					FloatInput("Half segment", &part.halfSegment, "m");
					int axis = static_cast<int>(part.axis);
					if (ChoiceInput("Capsule axis", &axis, "Y\0X\0Z\0"))
						part.axis = static_cast<CaseExecutionAxis>(axis);
					EndSettingGroup();
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (BeginSettingGroup("link_count", "Link count", PresenceStatus_Absent))
		{
			CountInput("Link count", &fixture.linkCount);
			EndSettingGroup();
		}
		for (std::uint32_t index = 0;
		     index < (std::min)(static_cast<std::uint32_t>(fixture.linkCount), kCaseExecutionRagdollLinkCapacity);
		     ++index)
		{
			ImGui::PushID(static_cast<int>(index));
			if (ImGui::TreeNode("Link", "Link %u", index))
			{
				if (BeginSettingGroup("link_properties", "Parent part"))
				{
					CountInput("Parent part", &fixture.links[index].parentPart);
					CountInput("Child part", &fixture.links[index].childPart);
					VectorInput("Anchor", &fixture.links[index].anchor);
					EndSettingGroup();
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
	}
	else if (spec->fixtureKind == CaseFixtureKind_LargePyramid)
	{
		CaseExecutionLargePyramid& fixture = spec->largePyramid;
		DrawPaneHeading("Pyramid and projectiles", 28);
		if (!BeginSettingGroup("pyramid_fixture", "Projectile launch velocity"))
			return;
		UnsignedInput("Row count", &fixture.rowCount);
		VectorInput("Box half extents", &fixture.boxHalfExtents);
		VectorInput("Box spacing", &fixture.boxSpacing);
		VectorInput("Base center", &fixture.baseCenter);
		VectorInput("Floor half extents", &fixture.floorHalfExtents);
		FloatInput("Box density", &fixture.boxDensity, "kg/m3");
		UnsignedInput("Projectile count", &fixture.projectileCount);
		FloatInput("Projectile radius", &fixture.projectileRadius, "m");
		FloatInput("Projectile density", &fixture.projectileDensity, "kg/m3");
		VectorInput("Projectile initial center", &fixture.projectileInitialCenter);
		VectorInput("Projectile center spacing", &fixture.projectileCenterSpacing);
		VectorInput("Projectile launch velocity", &fixture.projectileLaunchVelocity, "m/s");
		UnsignedInput("Projectile launch step", &fixture.projectileLaunchAfterWorkUnits);
		EndSettingGroup();
		if (spec->timestepHz != 0)
			ImGui::Text("Launch time %.3f s",
			            static_cast<double>(fixture.projectileLaunchAfterWorkUnits) / spec->timestepHz);
	}
	else if (spec->fixtureKind == CaseFixtureKind_PyramidWall)
	{
		CaseExecutionPyramidWall& fixture = spec->pyramidWall;
		DrawPaneHeading("Pyramid wall", 28);
		if (BeginSettingGroup("wall_fixture", "Floor half extents"))
		{
			UnsignedInput("Row count", &fixture.rowCount);
			FloatInput("Cube half extent", &fixture.halfExtent, "m");
			FloatInput("Density", &fixture.density, "kg/m3");
			VectorInput("Floor half extents", &fixture.floorHalfExtents);
			EndSettingGroup();
		}
	}
	else if (spec->fixtureKind == CaseFixtureKind_RayTracing)
	{
		CaseExecutionRayTracing& fixture = spec->rayTracing;
		DrawPaneHeading("Mixed ray scene", 28);
		if (BeginSettingGroup("ray_fixture", "Triangles per mesh", PresenceStatus_Absent))
		{
			UnsignedInput("Primitives", &fixture.primitiveCount);
			UnsignedInput("Static meshes", &fixture.meshCount);
			UnsignedInput("Triangles per mesh", &fixture.trianglesPerMesh);
			UnsignedInput("Kinematic bodies", &fixture.movingCount);
			EndSettingGroup();
		}
	}
}
}

void DrawEditableRunSettings(PhysicsArenaApp* app)
{
	using namespace physics_arena;
	RunSettings& settings = app->model.selection.settings;
	CaseExecutionSpec& spec = settings.caseInputs;
	if (spec.fixtureKind == CaseFixtureKind_RayTracing)
	{
		if (app->ui.runPage == NativeRunPage_Simulation)
		{
			DrawPaneHeading("Ray workload", 28);
			ImGui::TextWrapped("Ray queries do not use a dynamics solver");
			if (BeginSettingGroup("ray_schedule", "Measured suites"))
			{
				UnsignedInput("Warmup suites", &spec.warmupWorkUnitCount);
				UnsignedInput("Measured suites", &spec.measuredWorkUnitCount);
				UnsignedInput("Camera views", &spec.rayTracing.viewCount);
				UnsignedInput("Image width", &spec.rayTracing.width, "px");
				UnsignedInput("Image height", &spec.rayTracing.height, "px");
				EndSettingGroup();
			}
		}
		else if (app->ui.runPage == NativeRunPage_Fixture)
		{
			FixtureInputs(&spec);
		}
		return;
	}

	if (app->ui.runPage == NativeRunPage_Simulation)
	{
		DrawPaneHeading("Timing", 28);
		if (BeginSettingGroup("schedule", "Measured steps"))
		{
			if (spec.fixtureKind != CaseFixtureKind_SpatialQueryTrace)
				UnsignedInput("Simulation Hz", &spec.timestepHz, "Hz");
			const char* units = spec.fixtureKind == CaseFixtureKind_SpatialQueryTrace ? "batches" : "steps";
			UnsignedInput("Warmup", &spec.warmupWorkUnitCount, units);
			UnsignedInput("Measured", &spec.measuredWorkUnitCount, units);
			EndSettingGroup();
		}
		if (spec.fixtureKind == CaseFixtureKind_SpatialQueryTrace)
			ImGui::TextDisabled("Work unit: query batch");
		else if (spec.timestepHz != 0)
			ImGui::TextDisabled("Warmup %.3f s", static_cast<double>(spec.warmupWorkUnitCount) / spec.timestepHz);
		ImGui::Spacing();
		if (spec.fixtureKind != CaseFixtureKind_SpatialQueryTrace)
		{
			DrawPaneHeading("Environment", 28);
			if (BeginSettingGroup("physics", "Gravity"))
			{
				VectorInput("Gravity", &spec.gravity, "m/s2");
				EndSettingGroup();
			}
		}

	}
	if (app->ui.runPage == NativeRunPage_Fixture)
	{
		FixtureInputs(&spec);
	}
	if (app->ui.runPage != NativeRunPage_Simulation)
		return;
	if (spec.fixtureKind == CaseFixtureKind_SpatialQueryTrace)
	{
		ImGui::TextWrapped("Query batches do not use a dynamics solver");
		return;
	}
	DrawPaneHeading("Engine settings", 28);
	for (std::uint32_t ordinal = 0; ordinal < app->model.catalog.engineCount; ++ordinal)
	{
		const std::uint32_t index = app->model.engineDisplayOrder[ordinal];
		if (app->model.selection.engines[index] != PresenceStatus_Present)
			continue;
		const EngineRecord& engine = app->model.catalog.engines[index];
		const std::string_view name = CatalogTextView(&app->model.catalog, engine.displayName);
		std::array<char, 192> label = {};
		std::snprintf(label.data(), label.size(), "%.*s", static_cast<int>(name.size()), name.data());
		ImGui::PushID(static_cast<int>(index));
		if (ImGui::CollapsingHeader(label.data()))
		{
			if (ImGui::Button("Reset engine defaults"))
				ResetEngineRunSettings(&app->model.catalog, index, &settings);
			EngineRunSettings& profile = settings.engineSettings[index];
			const std::uint32_t fixture = 1u << spec.fixtureKind;
			const std::string_view engineId = CatalogTextView(&app->model.catalog, engine.id);
			const char* ccdHelp = "Continuous collision detection. Off selects discrete contacts, On selects native swept CCD";
			if (engineId == "unity_physics")
				ccdHelp = "Unity DOTS Physics has no selectable swept-CCD mode. Built-in predictive contacts remain active";
			else if (engineId == "avian3d")
				ccdHelp = "Continuous collision detection. Off disables sweeps. On sweeps against static and kinematic bodies with native thresholds";
			else if (engineId == "box3d")
				ccdHelp = "Continuous collision detection. On sweeps these non-bullet bodies against static targets";
			else if (engine.settings.continuousCollisionSemantics == ContinuousCollisionSemantics_PassiveContinuous)
				ccdHelp = "Continuous collision detection. Off: Passive, On: Continuous. Native speculative contacts remain part of Passive mode";
			else if (engine.settings.continuousCollisionSemantics == ContinuousCollisionSemantics_DiscreteLinearCast)
				ccdHelp = "Continuous collision detection. Off: Discrete, On: Linear cast. Native casting covers translation";
			if (BeginSettingGroup("engine_physics", "Linear damping"))
			{
				FloatInput("Friction", &profile.friction);
				if (engine.settings.maximumRestitution == 0)
					SettingStatus("Restitution", "Fixed at 0", engineId == "bepuphysics2"
					    ? "The stock BEPU contact material has no restitution coefficient. Spring contacts can still produce upward motion"
					    : "This adapter fixes restitution at zero");
				else
					FloatInput("Restitution", &profile.restitution);
				if ((engine.settings.sleepEnabledFixtures & fixture) == 0 || (engine.settings.sleepDisabledFixtures & fixture) == 0)
					SettingStatus("Sleep", engineId == "unity_physics" ? "Not supported" : "Unavailable for this case",
					    engineId == "unity_physics" ? "Unity DOTS Physics has no sleeping facility" : "This case does not expose both sleep modes");
				else
					ToggleInput("Sleep", &profile.sleepMode, "On permits native automatic sleeping. Off keeps bodies active");
				if ((engine.settings.continuousCollisionFixtures & fixture) == 0)
					SettingStatus("CCD", engineId == "unity_physics" ? "Not supported" : "Unavailable for this case", ccdHelp);
				else
					ToggleInput("CCD", &profile.continuousCollisionMode, ccdHelp);
				if (spec.fixtureKind == CaseFixtureKind_RagdollStairTumble)
				{
					if (engine.settings.ragdollDampingMode == RagdollDampingMode_FixedZero)
					{
						SettingStatus("Linear damping", "Fixed at 0", "This adapter fixes ragdoll damping at zero");
						SettingStatus("Angular damping", "Fixed at 0", "This adapter fixes ragdoll damping at zero");
					}
					else
					{
						const char* dampingHelp = engine.settings.ragdollDampingMode == RagdollDampingMode_PerSecondFraction
						    ? "Velocity loss per second. Values above one saturate"
						    : engineId == "box3d" || engineId == "rapier3d"
						        ? "Native damping coefficient. Each step multiplies velocity by 1 / (1 + coefficient * dt)"
						        : "Native damping coefficient. Each step multiplies velocity by max(0, 1 - coefficient * dt)";
						FloatInput("Linear damping", &profile.linearDamping, "", dampingHelp);
						FloatInput("Angular damping", &profile.angularDamping, "", dampingHelp);
					}
				}
				if ((engine.settings.solverStabilizationFixtures & fixture) != 0)
					ToggleInput("Stabilization", &profile.solverStabilization,
					    "Contact solver stabilization clips small velocities with one iteration. Later contact iterations adjust inertia. Extra substeps do not replace extra iterations");
				EndSettingGroup();
			}
			float widest = 0;
			std::string_view widestLabel = "";
			for (std::uint32_t field = 0; field < CaseSolverField_Count; ++field)
			{
				const std::string_view nativeName = CatalogTextView(&app->model.catalog, engine.settings.solver[field].nativeName);
				if ((engine.settings.solverFields & (1u << field)) != 0 && ImGui::CalcTextSize(nativeName.data(), nativeName.data() + nativeName.size()).x > widest)
				{
					widest = ImGui::CalcTextSize(nativeName.data(), nativeName.data() + nativeName.size()).x;
					widestLabel = nativeName;
				}
			}
			if (BeginSettingGroup("solver_properties", widestLabel, PresenceStatus_Absent))
			{
				for (std::uint32_t field = 0; field < CaseSolverField_Count; ++field)
				{
					if ((engine.settings.solverFields & (1u << field)) == 0)
						continue;
					const NativeSolverCapability& capability = engine.settings.solver[field];
					const std::string_view nativeName = CatalogTextView(&app->model.catalog, capability.nativeName);
					std::snprintf(label.data(), label.size(), "%.*s", static_cast<int>(nativeName.size()),
					              nativeName.data());
					std::array<char, 256> help = {};
					std::snprintf(help.data(), help.size(), "Native range %u..%u%s", capability.minimum, capability.maximum,
					    engineId == "rapier3d" && field == CaseSolverField_SolverIterations
					        ? "\nThis count selects temporal substeps, not an equivalent iteration count across engines" : "");
					UnsignedInput(label.data(), &profile.solver.values[field], "", help.data());
					if (engineId == "joltphysics" && field == CaseSolverField_VelocityIterations && profile.solver.values[field] < 2)
						ImGui::TextWrapped("Friction requires 2+ velocity iterations");
				}
				EndSettingGroup();
			}
			if ((engine.settings.solverFields &
			     ((1u << CaseSolverField_VelocityIterations) | (1u << CaseSolverField_Substeps))) ==
			    ((1u << CaseSolverField_VelocityIterations) | (1u << CaseSolverField_Substeps)))
				ImGui::Text("%u iterations/substep x %u substeps",
				            settings.engineSettings[index].solver.values[CaseSolverField_VelocityIterations],
				            settings.engineSettings[index].solver.values[CaseSolverField_Substeps]);
		}
		ImGui::PopID();
	}
}
}
