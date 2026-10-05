#pragma once

#include "replay_recording.h"
#include "stack_state_capture.h"

#include <array>

namespace box3d_benchmark
{
enum Box3DRayRunStage
{
	Box3DRayRunStage_Heavy,
	Box3DRayRunStage_Preflight
};

enum VerificationMode
{
	VerificationMode_On = 0,
	VerificationMode_Off = 1,
};

struct Box3DRunRequest
{
	VerificationMode verificationMode = VerificationMode_On;
	const char* outputPath;
	const char* stackStream;
	const char* rayCorpusPath;
	Box3DRayRunStage rayStage;
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

int ParseBox3DRunRequest(int argc, char** argv, Box3DRunRequest* request);
}
