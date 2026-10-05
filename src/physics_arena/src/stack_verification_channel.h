#pragma once

#include "stack_stability_internal.h"
#include <atomic>
#include <string>

namespace physics_arena
{
enum StackChannelState
{
	StackChannelState_Listening,
	StackChannelState_Streaming,
	StackChannelState_Footer,
	StackChannelState_Complete,
	StackChannelState_Failed,
};
struct StackVerificationChannel
{
	std::string endpoint;
	void* pipe;
	void* stopEvent;
	void* ioEvent;
	void* worker;
	std::atomic<StackChannelState> state;
	StackAssessmentState assessment;
};
ArenaStatus OpenStackVerificationChannel(const CaseExecutionSpec& execution, std::string_view runId,
                                        std::string_view engineId, std::uint32_t threads, std::uint32_t repeat,
                                        StackVerificationChannel* channel, StatusRecord* error);
void CloseStackVerificationChannel(StackVerificationChannel* channel);
}
