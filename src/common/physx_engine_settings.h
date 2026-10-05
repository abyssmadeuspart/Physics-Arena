#pragma once

#include "case_execution_wire.h"

namespace benchmark_physx
{
inline physx::PxFilterFlags EngineSimulationFilter(physx::PxFilterObjectAttributes attributes0,
    physx::PxFilterData filterData0, physx::PxFilterObjectAttributes attributes1,
    physx::PxFilterData filterData1, physx::PxPairFlags& pairFlags,
    const void* constantBlock, physx::PxU32 constantBlockSize)
{
	const physx::PxFilterFlags result = physx::PxDefaultSimulationFilterShader(attributes0, filterData0,
	    attributes1, filterData1, pairFlags, nullptr, 0);
	if (!result.isSet(physx::PxFilterFlag::eSUPPRESS) && !result.isSet(physx::PxFilterFlag::eKILL) &&
	    !physx::PxFilterObjectIsTrigger(attributes0) && !physx::PxFilterObjectIsTrigger(attributes1) &&
	    constantBlock != nullptr && constantBlockSize == sizeof(CaseExecutionToggle) &&
	    *static_cast<const CaseExecutionToggle*>(constantBlock) == CaseExecutionToggle_Enabled)
	{
		pairFlags |= physx::PxPairFlag::eDETECT_CCD_CONTACT;
	}
	return result;
}

inline void ApplyEngineSceneSettings(physx::PxSceneDesc* scene, const CaseExecutionSpec& execution)
{
	if (execution.continuousCollisionMode == CaseExecutionToggle_Enabled)
		scene->flags |= physx::PxSceneFlag::eENABLE_CCD;
	else
		scene->flags &= ~physx::PxSceneFlags(physx::PxSceneFlag::eENABLE_CCD);
	scene->filterShader = EngineSimulationFilter;
	scene->filterShaderData = &execution.continuousCollisionMode;
	scene->filterShaderDataSize = sizeof(execution.continuousCollisionMode);
}

inline void ApplyEngineBodySettings(physx::PxRigidDynamic* actor, const CaseExecutionSpec& execution)
{
	actor->setSolverIterationCounts(execution.nativeSolver.values[CaseSolverField_PositionIterations],
	    execution.nativeSolver.values[CaseSolverField_VelocityIterations]);
	actor->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD,
	    execution.continuousCollisionMode == CaseExecutionToggle_Enabled);
	if (execution.sleepMode == CaseExecutionToggle_Disabled)
		actor->setSleepThreshold(0.0f);
}
}
