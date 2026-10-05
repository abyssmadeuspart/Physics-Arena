#include "stack_verification_channel.h"
#include "run_internal.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace physics_arena
{
namespace
{
enum PipeTransfer
{
	PipeTransfer_Complete,
	PipeTransfer_End,
	PipeTransfer_Failed,
};

PipeTransfer FinishPipeOperation(StackVerificationChannel* channel, OVERLAPPED* operation, DWORD* transferred)
{
	const HANDLE waits[] = {channel->ioEvent, channel->stopEvent};
	if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0)
	{
		CancelIoEx(channel->pipe, operation);
		GetOverlappedResult(channel->pipe, operation, transferred, TRUE);
		return PipeTransfer_Failed;
	}
	if (GetOverlappedResult(channel->pipe, operation, transferred, FALSE) != 0) return PipeTransfer_Complete;
	return GetLastError() == ERROR_BROKEN_PIPE ? PipeTransfer_End : PipeTransfer_Failed;
}

PipeTransfer ReadStackBytes(StackVerificationChannel* channel, std::span<std::uint8_t> bytes)
{
	std::size_t offset = 0;
	while (offset < bytes.size())
	{
		OVERLAPPED operation = {};
		operation.hEvent = channel->ioEvent;
		ResetEvent(channel->ioEvent);
		DWORD transferred = 0;
		PipeTransfer status = PipeTransfer_Complete;
		if (ReadFile(channel->pipe, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), &transferred, &operation) == 0)
		{
			const DWORD error = GetLastError();
			status = error == ERROR_IO_PENDING ? FinishPipeOperation(channel, &operation, &transferred) :
			    error == ERROR_BROKEN_PIPE ? PipeTransfer_End : PipeTransfer_Failed;
		}
		if (status != PipeTransfer_Complete) return offset == 0 ? status : PipeTransfer_Failed;
		if (transferred == 0) return PipeTransfer_Failed;
		offset += transferred;
	}
	return PipeTransfer_Complete;
}

int ReplyStackRecord(StackVerificationChannel* channel, benchmark_stack::Reply reply)
{
	std::array<std::uint8_t, 4> bytes = {};
	benchmark_stack::EncodeU32(bytes.data(), static_cast<std::uint32_t>(reply));
	OVERLAPPED operation = {};
	operation.hEvent = channel->ioEvent;
	ResetEvent(channel->ioEvent);
	DWORD transferred = 0;
	if (WriteFile(channel->pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &transferred, &operation) == 0 &&
	    (GetLastError() != ERROR_IO_PENDING || FinishPipeOperation(channel, &operation, &transferred) != PipeTransfer_Complete)) return 2;
	return transferred == bytes.size() ? 0 : 2;
}

DWORD WINAPI AssessStackStream(void* opaque)
{
	StackVerificationChannel* channel = static_cast<StackVerificationChannel*>(opaque);
	OVERLAPPED connection = {};
	connection.hEvent = channel->ioEvent;
	DWORD transferred = 0;
	if (ConnectNamedPipe(channel->pipe, &connection) == 0)
	{
		const DWORD error = GetLastError();
		if (error != ERROR_PIPE_CONNECTED &&
		    (error != ERROR_IO_PENDING || FinishPipeOperation(channel, &connection, &transferred) != PipeTransfer_Complete))
		{
			channel->state.store(StackChannelState_Failed, std::memory_order_release);
			return 0;
		}
	}
	channel->state.store(StackChannelState_Streaming, std::memory_order_release);
	std::array<std::uint8_t, benchmark_stack::kHeaderBytes> header = {};
	int status = 2;
	if (ReadStackBytes(channel, header) == PipeTransfer_Complete)
	{
		const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		status = AcceptStackHeader(header, &channel->assessment);
		channel->assessment.result.analysisElapsedMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		if (ReplyStackRecord(channel, status == 0 ? benchmark_stack::Reply_Accepted : benchmark_stack::Reply_Rejected) != 0) status = 2;
	}
	if (status == 0)
	{
		std::vector<std::uint8_t> frame(benchmark_stack::kFrameHeaderBytes + static_cast<std::size_t>(benchmark_stack::kPoseBytes) * channel->assessment.execution.dynamicBodyCount);
		for (;;)
		{
			if (ReadStackBytes(channel, std::span<std::uint8_t>(frame.data(), 4)) != PipeTransfer_Complete) break;
			const std::uint32_t phase = static_cast<std::uint32_t>(frame[0]) | (static_cast<std::uint32_t>(frame[1]) << 8) |
			    (static_cast<std::uint32_t>(frame[2]) << 16) | (static_cast<std::uint32_t>(frame[3]) << 24);
			const std::size_t recordSize = phase == benchmark_stack::Phase_Complete ? benchmark_stack::kFooterBytes : frame.size();
			if (ReadStackBytes(channel, std::span<std::uint8_t>(frame.data() + 4, recordSize - 4)) != PipeTransfer_Complete) break;
			const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
			status = phase == benchmark_stack::Phase_Complete ? AcceptStackFooter(std::span<const std::uint8_t>(frame.data(), recordSize), &channel->assessment) : AssessStackFrame(frame, &channel->assessment);
			channel->assessment.result.analysisElapsedMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			if (status == 0 && phase == benchmark_stack::Phase_Complete) channel->state.store(StackChannelState_Footer, std::memory_order_release);
			if (ReplyStackRecord(channel, status == 0 ? benchmark_stack::Reply_Accepted : benchmark_stack::Reply_Rejected) != 0 || status != 0) break;
			if (phase == benchmark_stack::Phase_Complete)
			{
				std::array<std::uint8_t, 1> trailing = {};
				if (ReadStackBytes(channel, trailing) == PipeTransfer_End)
				{
					const std::chrono::steady_clock::time_point endStart = std::chrono::steady_clock::now();
					status = CompleteStackAssessment(&channel->assessment);
					channel->assessment.result.analysisElapsedMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - endStart).count();
					if (status == 0)
					{
						channel->state.store(StackChannelState_Complete, std::memory_order_release);
						return 0;
					}
				}
				break;
			}
		}
	}
	channel->state.store(StackChannelState_Failed, std::memory_order_release);
	DisconnectNamedPipe(channel->pipe);
	return 0;
}
}

ArenaStatus OpenStackVerificationChannel(const CaseExecutionSpec& execution, std::string_view runId,
                                        std::string_view engineId, std::uint32_t threads, std::uint32_t repeat,
                                        StackVerificationChannel* channel, StatusRecord* error)
{
	static std::atomic<std::uint64_t> serial = {};
	channel->endpoint = "\\\\.\\pipe\\PhysicsArena-stack-" + std::to_string(GetCurrentProcessId()) + "-" +
	    std::to_string(serial.fetch_add(1, std::memory_order_relaxed)) + "-" + std::to_string(threads) + "-" + std::to_string(repeat);
	InitializeStackAssessment(execution, runId, engineId, threads, repeat, &channel->assessment);
	channel->state.store(StackChannelState_Listening, std::memory_order_relaxed);
	channel->pipe = CreateNamedPipeA(channel->endpoint.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
	    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, nullptr);
	if (channel->pipe == INVALID_HANDLE_VALUE) channel->pipe = nullptr;
	channel->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	channel->ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (channel->pipe != nullptr && channel->stopEvent != nullptr && channel->ioEvent != nullptr)
		channel->worker = CreateThread(nullptr, 0, AssessStackStream, channel, 0, nullptr);
	if (channel->worker == nullptr)
	{
		CloseStackVerificationChannel(channel);
		return RunError(error, ArenaStatus_RunFailed, "stack_channel_create_failed");
	}
	return ArenaStatus_Ok;
}

void CloseStackVerificationChannel(StackVerificationChannel* channel)
{
	if (channel->worker != nullptr)
	{
		// allow the acknowledged footer to consume the producer's close before cancelling pending I/O
		if (channel->state.load(std::memory_order_acquire) != StackChannelState_Footer ||
		    WaitForSingleObject(channel->worker, 1000) != WAIT_OBJECT_0)
			SetEvent(channel->stopEvent);
		WaitForSingleObject(channel->worker, INFINITE);
		CloseHandle(channel->worker);
		channel->worker = nullptr;
	}
	for (void** handle : {&channel->pipe, &channel->stopEvent, &channel->ioEvent})
	{
		if (*handle != nullptr) CloseHandle(*handle);
		*handle = nullptr;
	}
}
}
