#include "box3d_visual_transport.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace box3d_benchmark
{
void CopyBox3DVisualProtocolText(char* target, const char* source)
{
	std::snprintf(target, benchmark_visual::kVisualBridgeTextCapacity, "%s", source);
}

int StartBox3DVisualSocketSystem()
{
#if defined(_WIN32)
	WSADATA data = {};
	return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : 2;
#else
	return 0;
#endif
}

void StopBox3DVisualSocketSystem()
{
#if defined(_WIN32)
	WSACleanup();
#endif
}

void CloseBox3DProducerSocket(Box3DProducerSocket socket)
{
	if (socket == kInvalidBox3DProducerSocket)
	{
		return;
	}
#if defined(_WIN32)
	closesocket(socket);
#else
	close(socket);
#endif
}

Box3DProducerSocket ConnectBox3DRenderer(const char* connectHost, int connectPort)
{
	Box3DProducerSocket socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socketHandle == kInvalidBox3DProducerSocket)
	{
		return kInvalidBox3DProducerSocket;
	}
	sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_port = htons(static_cast<uint16_t>(connectPort));
	if (inet_pton(AF_INET, connectHost, &address.sin_addr) != 1)
	{
		CloseBox3DProducerSocket(socketHandle);
		return kInvalidBox3DProducerSocket;
	}
	if (connect(socketHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
	{
		CloseBox3DProducerSocket(socketHandle);
		return kInvalidBox3DProducerSocket;
	}
	return socketHandle;
}

int SendBox3DVisualBytes(Box3DProducerSocket socket, const uint8_t* data, uint32_t byteCount)
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

int ReceiveBox3DVisualBytes(Box3DProducerSocket socket, uint8_t* data, uint32_t byteCount)
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

int SendBox3DVisualFrame(Box3DProducerSocket socket, benchmark_visual::VisualFrameType frameType, uint32_t sequence, const void* payload, uint32_t payloadBytes)
{
	benchmark_visual::VisualFrameHeader header = {};
	header.magic = benchmark_visual::kVisualTransportMagic;
	header.version = benchmark_visual::kVisualTransportVersion;
	header.frameType = static_cast<uint16_t>(frameType);
	header.sequence = sequence;
	header.payloadBytes = payloadBytes;
	header.headerCrc = benchmark_visual::VisualTransportHeaderCrc(header);
	int status = SendBox3DVisualBytes(socket, reinterpret_cast<const uint8_t*>(&header), sizeof(header));
	if (status != 0 || payloadBytes == 0u)
	{
		return status;
	}
	return SendBox3DVisualBytes(socket, static_cast<const uint8_t*>(payload), payloadBytes);
}

int ReceiveBox3DVisualFrame(Box3DProducerSocket socket, benchmark_visual::VisualFrameType expectedType, uint32_t expectedSequence, std::vector<uint8_t>& payload, uint32_t* payloadBytes)
{
	benchmark_visual::VisualFrameHeader header = {};
	if (ReceiveBox3DVisualBytes(socket, reinterpret_cast<uint8_t*>(&header), sizeof(header)) != 0)
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
	if (header.payloadBytes > 0u && ReceiveBox3DVisualBytes(socket, payload.data(), header.payloadBytes) != 0)
	{
		return 2;
	}
	*payloadBytes = header.payloadBytes;
	return 0;
}

int SendBox3DVisualCompleted(Box3DProducerSocket socket, benchmark_visual::VisualFrameType frameType, uint32_t sequence, uint32_t status, int completedStepCount, double physicsElapsedMs)
{
	benchmark_visual::VisualCompletedPayload completed = {};
	completed.status = status;
	completed.completedStepCount = static_cast<uint32_t>(completedStepCount);
	completed.physicsElapsedMs = physicsElapsedMs;
	return SendBox3DVisualFrame(socket, frameType, sequence, &completed, sizeof(completed));
}

int SendBox3DHelloAck(Box3DProducerSocket socket, const Box3DSharedVisualCli& cli, const Box3DCaseDescriptor& descriptor, uint32_t sequence)
{
	benchmark_visual::VisualHelloAckPayload ack = {};
	CopyBox3DVisualProtocolText(ack.engineId, kEngineId);
	CopyBox3DVisualProtocolText(ack.caseId, descriptor.caseId);
	CopyBox3DVisualProtocolText(ack.producerBuildId, "box3d_polygon_runner");
#if defined(_WIN32)
	ack.processId = static_cast<uint32_t>(GetCurrentProcessId());
#else
	ack.processId = static_cast<uint32_t>(getpid());
#endif
	ack.supportedFrameFlags = 0u;
	ack.runTokenHash = benchmark_visual::VisualTransportHashText(cli.runToken);
	ack.status = benchmark_visual::VisualTransportStatus_Ok;
	return SendBox3DVisualFrame(socket, benchmark_visual::VisualFrameType_HelloAck, sequence, &ack, sizeof(ack));
}

int DecodeBox3DHello(const Box3DSharedVisualCli& cli, const Box3DCaseDescriptor& descriptor, const std::vector<uint8_t>& payload, uint32_t payloadBytes)
{
	if (payloadBytes != sizeof(benchmark_visual::VisualHelloPayload))
	{
		return 2;
	}
	benchmark_visual::VisualHelloPayload hello = {};
	std::memcpy(&hello, payload.data(), sizeof(hello));
	if (std::strcmp(hello.runToken, cli.runToken) != 0 || std::strcmp(hello.engineId, cli.engineId) != 0 || std::strcmp(hello.caseId, descriptor.caseId) != 0)
	{
		return 2;
	}
	return 0;
}

int DecodeBox3DStart(const Box3DSharedVisualCli& cli, const Box3DCaseDescriptor& descriptor, const std::vector<uint8_t>& payload, uint32_t payloadBytes, Box3DCaseConfig* config)
{
	if (payloadBytes != sizeof(benchmark_visual::VisualStartPayload))
	{
		return 2;
	}
	benchmark_visual::VisualStartPayload start = {};
	std::memcpy(&start, payload.data(), sizeof(start));
	if (std::strcmp(start.config.engineId, cli.engineId) != 0 || std::strcmp(start.config.caseId, descriptor.caseId) != 0)
	{
		return 2;
	}
	config->threadCount = static_cast<int>(start.config.threadCount);
	config->repeatIndex = static_cast<int>(start.config.repeatIndex);
	config->stepCount = static_cast<int>(start.config.stepCount);
	config->warmupSteps = static_cast<int>(start.config.warmupSteps);
	return 0;
}

int DecodeBox3DStep(const std::vector<uint8_t>& payload, uint32_t payloadBytes, int* stepCount)
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
}
