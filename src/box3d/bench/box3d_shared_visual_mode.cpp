#include "box3d_shared_visual_mode.h"

#include "box3d_case_registry.h"
#include "box3d_visual_cli.h"
#include "box3d_visual_snapshot.h"
#include "box3d_visual_transport.h"

#include <cstdio>
#include <vector>

namespace box3d_benchmark
{
int RunSharedVisualLoop(const Box3DSharedVisualCli& cli, const Box3DCaseDescriptor& descriptor)
{
	int exitCode = 2;
	int socketSystemStarted = 0;
	int stateCreated = 0;
	Box3DProducerSocket socket = kInvalidBox3DProducerSocket;
	Box3DCaseConfig config = {};
	Box3DCaseState state = {};
	std::vector<uint8_t> payload(benchmark_visual::kMaxFramePayloadBytes);
	std::vector<Box3DTransform> caseTransforms(descriptor.dynamicBodyCount);
	std::vector<Box3DStaticBox> caseBoxes(descriptor.staticBodyCount);
	uint32_t payloadBytes = 0u;
	int warmupStepCount = 0;
	int warmupStatus = 0;

	if (StartBox3DVisualSocketSystem() != 0)
	{
		std::fprintf(stderr, "run_failed reason=socket_system\n");
		goto cleanup;
	}
	socketSystemStarted = 1;

	socket = ConnectBox3DRenderer(cli.connectHost, cli.connectPort);
	if (socket == kInvalidBox3DProducerSocket)
	{
		std::fprintf(stderr, "run_failed reason=connect\n");
		goto cleanup;
	}

	if (ReceiveBox3DVisualFrame(socket, benchmark_visual::VisualFrameType_Hello, 1u, payload, &payloadBytes) != 0 ||
		DecodeBox3DHello(cli, descriptor, payload, payloadBytes) != 0 ||
		SendBox3DHelloAck(socket, cli, descriptor, 1u) != 0)
	{
		std::fprintf(stderr, "run_failed reason=hello_handshake\n");
		goto cleanup;
	}

	if (ReceiveBox3DVisualFrame(socket, benchmark_visual::VisualFrameType_Start, 2u, payload, &payloadBytes) != 0 ||
		DecodeBox3DStart(cli, descriptor, payload, payloadBytes, &config) != 0)
	{
		std::fprintf(stderr, "run_failed reason=start_frame\n");
		goto cleanup;
	}

	if (descriptor.CreateState(config, &state) != 0)
	{
		std::fprintf(stderr, "run_failed reason=create_state\n");
		SendBox3DVisualCompleted(socket, benchmark_visual::VisualFrameType_Started, 2u, benchmark_visual::VisualTransportStatus_ProducerFailed, 0, 0.0);
		goto cleanup;
	}
	stateCreated = 1;

	if (SendBox3DVisualCompleted(socket, benchmark_visual::VisualFrameType_Started, 2u, benchmark_visual::VisualTransportStatus_Ok, 0, 0.0) != 0)
	{
		std::fprintf(stderr, "run_failed reason=send_started\n");
		goto cleanup;
	}

	if (ReceiveBox3DVisualFrame(socket, benchmark_visual::VisualFrameType_Warmup, 3u, payload, &payloadBytes) != 0 ||
		DecodeBox3DStep(payload, payloadBytes, &warmupStepCount) != 0)
	{
		std::fprintf(stderr, "run_failed reason=warmup_frame\n");
		goto cleanup;
	}

	warmupStatus = descriptor.RunWarmup(config);
	if (SendBox3DVisualCompleted(socket, benchmark_visual::VisualFrameType_Completed, 3u, warmupStatus == 0 ? benchmark_visual::VisualTransportStatus_Ok : benchmark_visual::VisualTransportStatus_ProducerFailed, 0, 0.0) != 0 || warmupStatus != 0)
	{
		std::fprintf(stderr, "run_failed reason=warmup status=%d\n", warmupStatus);
		goto cleanup;
	}

	for (uint32_t sequence = 4u;; ++sequence)
	{
		benchmark_visual::VisualFrameHeader peekHeader = {};
		if (ReceiveBox3DVisualBytes(socket, reinterpret_cast<uint8_t*>(&peekHeader), sizeof(peekHeader)) != 0)
		{
			std::fprintf(stderr, "run_failed reason=receive_frame_header sequence=%u\n", sequence);
			break;
		}
		if (peekHeader.magic != benchmark_visual::kVisualTransportMagic || peekHeader.version != benchmark_visual::kVisualTransportVersion ||
			peekHeader.sequence != sequence || peekHeader.payloadBytes > payload.size() || peekHeader.headerCrc != benchmark_visual::VisualTransportHeaderCrc(peekHeader))
		{
			std::fprintf(stderr, "run_failed reason=invalid_frame_header sequence=%u frame_type=%u payload_bytes=%u\n", sequence, peekHeader.frameType, peekHeader.payloadBytes);
			break;
		}
		if (peekHeader.payloadBytes > 0u && ReceiveBox3DVisualBytes(socket, payload.data(), peekHeader.payloadBytes) != 0)
		{
			std::fprintf(stderr, "run_failed reason=receive_frame_payload sequence=%u\n", sequence);
			break;
		}
		if (peekHeader.frameType == benchmark_visual::VisualFrameType_Shutdown)
		{
			if (SendBox3DVisualCompleted(socket, benchmark_visual::VisualFrameType_Completed, sequence, benchmark_visual::VisualTransportStatus_Ok, state.completedStepCount, state.physicsElapsedMs) != 0)
			{
				std::fprintf(stderr, "run_failed reason=send_shutdown_completed sequence=%u\n", sequence);
				break;
			}
			exitCode = 0;
			goto cleanup;
		}
		if (peekHeader.frameType != benchmark_visual::VisualFrameType_Step)
		{
			std::fprintf(stderr, "run_failed reason=unexpected_frame frame_type=%u sequence=%u\n", peekHeader.frameType, sequence);
			break;
		}
		int stepCount = 0;
		if (DecodeBox3DStep(payload, peekHeader.payloadBytes, &stepCount) != 0 || descriptor.Step(&state, stepCount) != 0)
		{
			std::fprintf(stderr, "run_failed reason=step sequence=%u\n", sequence);
			break;
		}
		uint32_t snapshotPayloadBytes = BuildBox3DVisualSnapshotPayload(descriptor, state, caseTransforms.data(), caseBoxes.data(), payload);
		if (SendBox3DVisualFrame(socket, benchmark_visual::VisualFrameType_Snapshot, sequence, payload.data(), snapshotPayloadBytes) != 0)
		{
			std::fprintf(stderr, "run_failed reason=send_snapshot sequence=%u\n", sequence);
			break;
		}
	}

cleanup:
	if (stateCreated != 0)
	{
		descriptor.DestroyState(&state);
	}
	CloseBox3DProducerSocket(socket);
	if (socketSystemStarted != 0)
	{
		StopBox3DVisualSocketSystem();
	}
	return exitCode;
}
}

namespace box3d_benchmark
{
int RunSharedVisualMode(int argc, char** argv)
{
	Box3DSharedVisualCli cli = {};
	if (ParseBox3DSharedVisualCli(argc, argv, &cli) != 0)
	{
		return 2;
	}
	const Box3DCaseDescriptor* descriptor = nullptr;
	if (ResolveBox3DCase(cli.caseId, &descriptor) != 0)
	{
		std::fprintf(stderr, "invalid_argument name=case value=%s\n", cli.caseId);
		return 2;
	}
	return RunSharedVisualLoop(cli, *descriptor);
}
}
