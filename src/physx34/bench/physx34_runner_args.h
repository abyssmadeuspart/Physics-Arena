#pragma once

#include "replay_recording.h"
#include "stack_state_capture.h"

#include <array>

namespace physx34_benchmark
{
enum VerificationMode
{
	VerificationMode_On = 0,
	VerificationMode_Off = 1,
};

struct PhysXRunRequest
{
	VerificationMode verificationMode = VerificationMode_On;
	const char* outputPath;
	const char* stackStream;
	const char* stepTimingOutputPath;
	const char* recordingPath;
	benchmark_replay::RecordingMode recordingMode;
	CaseExecutionSpec caseExecution;
	std::array<std::uint8_t, kCaseExecutionPayloadCapacity> caseExecutionBytes;
	int threadCount;
	int stepCount;
	int warmupSteps;
	int repeatIndex;
};

int ParsePhysXRunRequest(int argc, char** argv, PhysXRunRequest* request);
}
