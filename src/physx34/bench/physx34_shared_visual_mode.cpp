#include "physx34_shared_visual_mode.h"

#include "physx34_case_registry.h"
#include "benchmark_visual/visual_transport.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace
{
#if defined(_WIN32)
using VisualSocket = SOCKET;
constexpr VisualSocket kInvalidVisualSocket = INVALID_SOCKET;
#else
using VisualSocket = int;
constexpr VisualSocket kInvalidVisualSocket = -1;
#endif

struct SharedVisualCli
{
	const char* producerMode;
	const char* connectHost;
	const char* runToken;
	const char* engineId;
	const char* caseId;
	int connectPort;
	int threadCount;
	int repeatIndex;
	int stepCount;
	int warmupSteps;
};

int ParseInt(const char* value, int minimumValue, int* parsed)
{
	int result = 0;
	for (int index = 0; value[index] != '\0'; ++index)
	{
		if (value[index] < '0' || value[index] > '9')
		{
			return 2;
		}
		result = (result * 10) + (value[index] - '0');
		if (result > 1000000)
		{
			return 2;
		}
	}
	if (result < minimumValue)
	{
		return 2;
	}
	*parsed = result;
	return 0;
}

int ParseArgs(int argc, char** argv, SharedVisualCli* cli)
{
	cli->producerMode = "";
	cli->connectHost = "127.0.0.1";
	cli->runToken = "";
	cli->engineId = "";
	cli->caseId = "";
	cli->connectPort = 0;
	cli->threadCount = 1;
	cli->repeatIndex = 0;
	cli->stepCount = 300;
	cli->warmupSteps = 0;
	for (int index = 1; index < argc; ++index)
	{
		const char* arg = argv[index];
		if (std::strncmp(arg, "--producer-mode=", 16) == 0)
		{
			cli->producerMode = arg + 16;
		}
		else if (std::strncmp(arg, "--connect-host=", 15) == 0)
		{
			cli->connectHost = arg + 15;
		}
		else if (std::strncmp(arg, "--connect-port=", 15) == 0)
		{
			if (ParseInt(arg + 15, 1, &cli->connectPort) != 0)
			{
				return 2;
			}
		}
		else if (std::strncmp(arg, "--run-token=", 12) == 0)
		{
			cli->runToken = arg + 12;
		}
		else if (std::strncmp(arg, "--engine=", 9) == 0)
		{
			cli->engineId = arg + 9;
		}
		else if (std::strncmp(arg, "--case=", 7) == 0)
		{
			cli->caseId = arg + 7;
		}
		else if (std::strncmp(arg, "--thread-count=", 15) == 0)
		{
			if (ParseInt(arg + 15, 1, &cli->threadCount) != 0)
			{
				return 2;
			}
		}
		else if (std::strncmp(arg, "--repeat-index=", 15) == 0)
		{
			if (ParseInt(arg + 15, 0, &cli->repeatIndex) != 0)
			{
				return 2;
			}
		}
		else if (std::strncmp(arg, "--step-count=", 13) == 0)
		{
			if (ParseInt(arg + 13, 1, &cli->stepCount) != 0)
			{
				return 2;
			}
		}
		else if (std::strncmp(arg, "--warmup-steps=", 15) == 0)
		{
			if (ParseInt(arg + 15, 0, &cli->warmupSteps) != 0)
			{
				return 2;
			}
		}
		else
		{
			std::fprintf(stderr, "invalid_argument value=%s\n", arg);
			return 2;
		}
	}
	if (std::strcmp(cli->producerMode, "shared-visual") != 0 || std::strcmp(cli->connectHost, "127.0.0.1") != 0 ||
		std::strcmp(cli->engineId, physx34_benchmark::kEngineId) != 0 || cli->caseId[0] == '\0' || cli->runToken[0] == '\0' ||
		cli->connectPort <= 0)
	{
		std::fprintf(stderr, "invalid_argument reason=shared_visual_producer_contract\n");
		return 2;
	}
	return 0;
}

void CopyText(char* target, const char* source)
{
	std::snprintf(target, benchmark_visual::kVisualBridgeTextCapacity, "%s", source);
}

int StartSocketSystem()
{
#if defined(_WIN32)
	WSADATA data = {};
	return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : 2;
#else
	return 0;
#endif
}

void StopSocketSystem()
{
#if defined(_WIN32)
	WSACleanup();
#endif
}

void CloseVisualSocket(VisualSocket socket)
{
	if (socket == kInvalidVisualSocket)
	{
		return;
	}
#if defined(_WIN32)
	closesocket(socket);
#else
	close(socket);
#endif
}

VisualSocket ConnectRenderer(const SharedVisualCli& cli)
{
	VisualSocket socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socketHandle == kInvalidVisualSocket)
	{
		return kInvalidVisualSocket;
	}
	sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_port = htons(static_cast<uint16_t>(cli.connectPort));
	if (inet_pton(AF_INET, cli.connectHost, &address.sin_addr) != 1)
	{
		CloseVisualSocket(socketHandle);
		return kInvalidVisualSocket;
	}
	if (connect(socketHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
	{
		CloseVisualSocket(socketHandle);
		return kInvalidVisualSocket;
	}
	return socketHandle;
}

int SendAll(VisualSocket socket, const uint8_t* data, uint32_t byteCount)
{
	uint32_t sent = 0u;
	while (sent < byteCount)
	{
		int result = send(socket, reinterpret_cast<const char*>(data + sent), static_cast<int>(byteCount - sent), 0);
		if (result <= 0)
		{
			return 2;
		}
		sent += static_cast<uint32_t>(result);
	}
	return 0;
}

int ReceiveAll(VisualSocket socket, uint8_t* data, uint32_t byteCount)
{
	uint32_t received = 0u;
	while (received < byteCount)
	{
		int result = recv(socket, reinterpret_cast<char*>(data + received), static_cast<int>(byteCount - received), 0);
		if (result <= 0)
		{
			return 2;
		}
		received += static_cast<uint32_t>(result);
	}
	return 0;
}

int SendFrame(VisualSocket socket, benchmark_visual::VisualFrameType frameType, uint32_t sequence, const void* payload, uint32_t payloadBytes)
{
	benchmark_visual::VisualFrameHeader header = {};
	header.magic = benchmark_visual::kVisualTransportMagic;
	header.version = benchmark_visual::kVisualTransportVersion;
	header.frameType = static_cast<uint16_t>(frameType);
	header.sequence = sequence;
	header.payloadBytes = payloadBytes;
	header.headerCrc = benchmark_visual::VisualTransportHeaderCrc(header);
	int status = SendAll(socket, reinterpret_cast<const uint8_t*>(&header), sizeof(header));
	if (status != 0 || payloadBytes == 0u)
	{
		return status;
	}
	return SendAll(socket, static_cast<const uint8_t*>(payload), payloadBytes);
}

int ReceiveFrame(VisualSocket socket, benchmark_visual::VisualFrameType expectedType, uint32_t expectedSequence, std::vector<uint8_t>& payload, uint32_t* payloadBytes)
{
	benchmark_visual::VisualFrameHeader header = {};
	if (ReceiveAll(socket, reinterpret_cast<uint8_t*>(&header), sizeof(header)) != 0)
	{
		return 2;
	}
	if (header.magic != benchmark_visual::kVisualTransportMagic || header.version != benchmark_visual::kVisualTransportVersion ||
		header.frameType != static_cast<uint16_t>(expectedType) || header.sequence != expectedSequence ||
		header.payloadBytes > benchmark_visual::kMaxFramePayloadBytes || header.headerCrc != benchmark_visual::VisualTransportHeaderCrc(header))
	{
		return 2;
	}
	if (header.payloadBytes > payload.size())
	{
		return 2;
	}
	if (header.payloadBytes > 0u && ReceiveAll(socket, payload.data(), header.payloadBytes) != 0)
	{
		return 2;
	}
	*payloadBytes = header.payloadBytes;
	return 0;
}

int SendCompleted(VisualSocket socket, benchmark_visual::VisualFrameType frameType, uint32_t sequence, uint32_t status, int completedStepCount, double physicsElapsedMs)
{
	benchmark_visual::VisualCompletedPayload completed = {};
	completed.status = status;
	completed.completedStepCount = static_cast<uint32_t>(completedStepCount);
	completed.physicsElapsedMs = physicsElapsedMs;
	return SendFrame(socket, frameType, sequence, &completed, sizeof(completed));
}

int SendHelloAck(VisualSocket socket, const SharedVisualCli& cli, const physx34_benchmark::PhysXCaseDescriptor& descriptor, uint32_t sequence)
{
	benchmark_visual::VisualHelloAckPayload ack = {};
	CopyText(ack.engineId, physx34_benchmark::kEngineId);
	CopyText(ack.caseId, descriptor.caseId);
	CopyText(ack.producerBuildId, "physx34_polygon_runner");
#if defined(_WIN32)
	ack.processId = static_cast<uint32_t>(GetCurrentProcessId());
#else
	ack.processId = static_cast<uint32_t>(getpid());
#endif
	ack.supportedFrameFlags = 0u;
	ack.runTokenHash = benchmark_visual::VisualTransportHashText(cli.runToken);
	ack.status = benchmark_visual::VisualTransportStatus_Ok;
	return SendFrame(socket, benchmark_visual::VisualFrameType_HelloAck, sequence, &ack, sizeof(ack));
}

int DecodeHello(const SharedVisualCli& cli, const std::vector<uint8_t>& payload, uint32_t payloadBytes)
{
	if (payloadBytes != sizeof(benchmark_visual::VisualHelloPayload))
	{
		return 2;
	}
	benchmark_visual::VisualHelloPayload hello = {};
	std::memcpy(&hello, payload.data(), sizeof(hello));
	if (std::strcmp(hello.runToken, cli.runToken) != 0 || std::strcmp(hello.engineId, cli.engineId) != 0 || std::strcmp(hello.caseId, cli.caseId) != 0)
	{
		return 2;
	}
	return 0;
}

int DecodeStart(const SharedVisualCli& cli, const std::vector<uint8_t>& payload, uint32_t payloadBytes, physx34_benchmark::PhysXCaseConfig* config)
{
	if (payloadBytes != sizeof(benchmark_visual::VisualStartPayload))
	{
		return 2;
	}
	benchmark_visual::VisualStartPayload start = {};
	std::memcpy(&start, payload.data(), sizeof(start));
	if (std::strcmp(start.config.engineId, cli.engineId) != 0 || std::strcmp(start.config.caseId, cli.caseId) != 0)
	{
		return 2;
	}
	config->threadCount = static_cast<int>(start.config.threadCount);
	config->repeatIndex = static_cast<int>(start.config.repeatIndex);
	config->stepCount = static_cast<int>(start.config.stepCount);
	config->warmupSteps = static_cast<int>(start.config.warmupSteps);
	return 0;
}

int DecodeStep(const std::vector<uint8_t>& payload, uint32_t payloadBytes, int* stepCount)
{
	if (payloadBytes != sizeof(benchmark_visual::VisualStepPayload))
	{
		return 2;
	}
	benchmark_visual::VisualStepPayload step = {};
	std::memcpy(&step, payload.data(), sizeof(step));
	*stepCount = static_cast<int>(step.commandStepCount);
	return *stepCount >= 0 ? 0 : 2;
}

void FillIdentity(const physx34_benchmark::PhysXCaseDescriptor& descriptor, const physx34_benchmark::PhysXCaseConfig& config, benchmark_visual::VisualIdentity* identity)
{
	CopyText(identity->caseId, descriptor.caseId);
	CopyText(identity->engineId, physx34_benchmark::kEngineId);
	CopyText(identity->fixtureSemantic, descriptor.fixtureSemantic);
	CopyText(identity->fixtureVersion, descriptor.fixtureVersion);
	identity->threadCount = static_cast<uint32_t>(config.threadCount);
	identity->repeatIndex = static_cast<uint32_t>(config.repeatIndex);
	identity->stepCount = static_cast<uint32_t>(config.stepCount);
	identity->warmupSteps = static_cast<uint32_t>(config.warmupSteps);
	identity->bodyCount = static_cast<uint32_t>(descriptor.bodyCount);
	identity->shapeCount = static_cast<uint32_t>(descriptor.bodyCount);
	identity->staticBoxCount = static_cast<uint32_t>(descriptor.staticBodyCount);
}

uint32_t BuildSnapshotPayload(
	const physx34_benchmark::PhysXCaseDescriptor& descriptor,
	const physx34_benchmark::PhysXCaseState& state,
	physx34_benchmark::PhysXTransform* caseTransforms,
	physx34_benchmark::PhysXStaticBox* caseBoxes,
	std::vector<uint8_t>& payload)
{
	descriptor.SampleTransforms(state, caseTransforms, descriptor.dynamicBodyCount);
	descriptor.CopyStaticBoxes(caseBoxes, descriptor.staticBodyCount);
	benchmark_visual::VisualSnapshotPayloadHeader header = {};
	FillIdentity(descriptor, state.config, &header.identity);
	header.timing.physicsElapsedMs = state.physicsElapsedMs;
	header.timing.transportElapsedMs = 0.0;
	header.timing.producerWaitMs = 0.0;
	header.timing.renderElapsedMs = 0.0;
	header.timing.presentWaitMs = 0.0;
	header.transformCount = static_cast<uint32_t>(descriptor.dynamicBodyCount);
	header.staticBoxCount = static_cast<uint32_t>(descriptor.staticBodyCount);
	header.completedStepCount = static_cast<uint32_t>(state.completedStepCount);
	header.status = benchmark_visual::VisualTransportStatus_Ok;
	header.dynamicHalfExtentX = descriptor.dynamicHalfExtent;
	header.dynamicHalfExtentY = descriptor.dynamicHalfExtent;
	header.dynamicHalfExtentZ = descriptor.dynamicHalfExtent;
	uint32_t payloadBytes = static_cast<uint32_t>(sizeof(header)) +
		static_cast<uint32_t>(descriptor.dynamicBodyCount * sizeof(benchmark_visual::VisualTransform)) +
		static_cast<uint32_t>(descriptor.staticBodyCount * sizeof(benchmark_visual::VisualStaticBox));
	uint8_t* cursor = payload.data();
	std::memcpy(cursor, &header, sizeof(header));
	cursor += sizeof(header);
	for (int index = 0; index < descriptor.dynamicBodyCount; ++index)
	{
		benchmark_visual::VisualTransform transform = {
			caseTransforms[index].positionX,
			caseTransforms[index].positionY,
			caseTransforms[index].positionZ,
			caseTransforms[index].rotationX,
			caseTransforms[index].rotationY,
			caseTransforms[index].rotationZ,
			caseTransforms[index].rotationW,
		};
		std::memcpy(cursor, &transform, sizeof(transform));
		cursor += sizeof(transform);
	}
	for (int index = 0; index < descriptor.staticBodyCount; ++index)
	{
		benchmark_visual::VisualStaticBox staticBox = {
			caseBoxes[index].positionX,
			caseBoxes[index].positionY,
			caseBoxes[index].positionZ,
			caseBoxes[index].halfExtentX,
			caseBoxes[index].halfExtentY,
			caseBoxes[index].halfExtentZ,
		};
		std::memcpy(cursor, &staticBox, sizeof(staticBox));
		cursor += sizeof(staticBox);
	}
	return payloadBytes;
}

int RunSharedVisualLoop(const SharedVisualCli& cli)
{
	const physx34_benchmark::PhysXCaseDescriptor* descriptor = nullptr;
	if (physx34_benchmark::ResolvePhysXCase(cli.caseId, &descriptor) != 0)
	{
		std::fprintf(stderr, "invalid_argument name=case value=%s\n", cli.caseId);
		return 2;
	}

	int exitCode = 2;
	int socketSystemStarted = 0;
	int stateCreated = 0;
	VisualSocket socket = kInvalidVisualSocket;
	physx34_benchmark::PhysXCaseConfig config = {};
	physx34_benchmark::PhysXCaseState state = {};
	std::vector<uint8_t> payload(benchmark_visual::kMaxFramePayloadBytes);
	std::vector<physx34_benchmark::PhysXTransform> caseTransforms(descriptor->dynamicBodyCount);
	std::vector<physx34_benchmark::PhysXStaticBox> caseBoxes(descriptor->staticBodyCount);
	uint32_t payloadBytes = 0u;
	int warmupStepCount = 0;
	int warmupStatus = 0;

	if (StartSocketSystem() != 0)
	{
		std::fprintf(stderr, "run_failed reason=socket_system\n");
		goto cleanup;
	}
	socketSystemStarted = 1;

	socket = ConnectRenderer(cli);
	if (socket == kInvalidVisualSocket)
	{
		std::fprintf(stderr, "run_failed reason=connect\n");
		goto cleanup;
	}

	if (ReceiveFrame(socket, benchmark_visual::VisualFrameType_Hello, 1u, payload, &payloadBytes) != 0 || DecodeHello(cli, payload, payloadBytes) != 0 || SendHelloAck(socket, cli, *descriptor, 1u) != 0)
	{
		std::fprintf(stderr, "run_failed reason=hello_handshake\n");
		goto cleanup;
	}

	if (ReceiveFrame(socket, benchmark_visual::VisualFrameType_Start, 2u, payload, &payloadBytes) != 0 || DecodeStart(cli, payload, payloadBytes, &config) != 0)
	{
		std::fprintf(stderr, "run_failed reason=start_frame\n");
		goto cleanup;
	}

	if (descriptor->CreateState(config, &state) != 0)
	{
		std::fprintf(stderr, "run_failed reason=create_state\n");
		SendCompleted(socket, benchmark_visual::VisualFrameType_Started, 2u, benchmark_visual::VisualTransportStatus_ProducerFailed, 0, 0.0);
		goto cleanup;
	}
	stateCreated = 1;

	if (SendCompleted(socket, benchmark_visual::VisualFrameType_Started, 2u, benchmark_visual::VisualTransportStatus_Ok, 0, 0.0) != 0)
	{
		std::fprintf(stderr, "run_failed reason=send_started\n");
		goto cleanup;
	}

	if (ReceiveFrame(socket, benchmark_visual::VisualFrameType_Warmup, 3u, payload, &payloadBytes) != 0 || DecodeStep(payload, payloadBytes, &warmupStepCount) != 0)
	{
		std::fprintf(stderr, "run_failed reason=warmup_frame\n");
		goto cleanup;
	}

	if (warmupStepCount > 0)
	{
		warmupStatus = descriptor->Step(&state, warmupStepCount);
		if (stateCreated != 0)
		{
			descriptor->DestroyState(&state);
			stateCreated = 0;
		}
		if (warmupStatus == 0)
		{
			warmupStatus = descriptor->CreateState(config, &state);
			stateCreated = warmupStatus == 0 ? 1 : 0;
		}
	}

	if (SendCompleted(socket, benchmark_visual::VisualFrameType_Completed, 3u, warmupStatus == 0 ? benchmark_visual::VisualTransportStatus_Ok : benchmark_visual::VisualTransportStatus_ProducerFailed, 0, 0.0) != 0 || warmupStatus != 0)
	{
		std::fprintf(stderr, "run_failed reason=warmup status=%d\n", warmupStatus);
		goto cleanup;
	}

	for (uint32_t sequence = 4u;; ++sequence)
	{
		benchmark_visual::VisualFrameHeader peekHeader = {};
		if (ReceiveAll(socket, reinterpret_cast<uint8_t*>(&peekHeader), sizeof(peekHeader)) != 0)
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
		if (peekHeader.payloadBytes > 0u && ReceiveAll(socket, payload.data(), peekHeader.payloadBytes) != 0)
		{
			std::fprintf(stderr, "run_failed reason=receive_frame_payload sequence=%u\n", sequence);
			break;
		}
		if (peekHeader.frameType == benchmark_visual::VisualFrameType_Shutdown)
		{
			if (SendCompleted(socket, benchmark_visual::VisualFrameType_Completed, sequence, benchmark_visual::VisualTransportStatus_Ok, state.completedStepCount, state.physicsElapsedMs) != 0)
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
		if (DecodeStep(payload, peekHeader.payloadBytes, &stepCount) != 0 || descriptor->Step(&state, stepCount) != 0)
		{
			std::fprintf(stderr, "run_failed reason=step sequence=%u\n", sequence);
			break;
		}
		uint32_t snapshotPayloadBytes = BuildSnapshotPayload(*descriptor, state, caseTransforms.data(), caseBoxes.data(), payload);
		if (SendFrame(socket, benchmark_visual::VisualFrameType_Snapshot, sequence, payload.data(), snapshotPayloadBytes) != 0)
		{
			std::fprintf(stderr, "run_failed reason=send_snapshot sequence=%u\n", sequence);
			break;
		}
	}

cleanup:
	if (stateCreated != 0)
	{
		descriptor->DestroyState(&state);
	}
	CloseVisualSocket(socket);
	if (socketSystemStarted != 0)
	{
		StopSocketSystem();
	}
	return exitCode;
}
}

namespace physx34_benchmark
{
int RunSharedVisualMode(int argc, char** argv)
{
	SharedVisualCli cli = {};
	if (ParseArgs(argc, argv, &cli) != 0)
	{
		return 2;
	}
	return RunSharedVisualLoop(cli);
}
}
