#pragma once

#include "box3d_case_registry.h"
#include "box3d_visual_cli.h"
#include "benchmark_visual/visual_transport.h"

#include <cstdint>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
namespace box3d_benchmark
{
using Box3DProducerSocket = SOCKET;
constexpr Box3DProducerSocket kInvalidBox3DProducerSocket = INVALID_SOCKET;
}
#else
namespace box3d_benchmark
{
using Box3DProducerSocket = int;
constexpr Box3DProducerSocket kInvalidBox3DProducerSocket = -1;
}
#endif

namespace box3d_benchmark
{
int StartBox3DVisualSocketSystem();
void StopBox3DVisualSocketSystem();
void CloseBox3DProducerSocket(Box3DProducerSocket socket);
Box3DProducerSocket ConnectBox3DRenderer(const char* connectHost, int connectPort);
int SendBox3DVisualFrame(Box3DProducerSocket socket, benchmark_visual::VisualFrameType frameType, uint32_t sequence, const void* payload, uint32_t payloadBytes);
int ReceiveBox3DVisualFrame(Box3DProducerSocket socket, benchmark_visual::VisualFrameType expectedType, uint32_t expectedSequence, std::vector<uint8_t>& payload, uint32_t* payloadBytes);
int ReceiveBox3DVisualBytes(Box3DProducerSocket socket, uint8_t* data, uint32_t byteCount);
int SendBox3DVisualCompleted(Box3DProducerSocket socket, benchmark_visual::VisualFrameType frameType, uint32_t sequence, uint32_t status, int completedStepCount, double physicsElapsedMs);
int SendBox3DHelloAck(Box3DProducerSocket socket, const Box3DSharedVisualCli& cli, const Box3DCaseDescriptor& descriptor, uint32_t sequence);
int DecodeBox3DHello(const Box3DSharedVisualCli& cli, const Box3DCaseDescriptor& descriptor, const std::vector<uint8_t>& payload, uint32_t payloadBytes);
int DecodeBox3DStart(const Box3DSharedVisualCli& cli, const Box3DCaseDescriptor& descriptor, const std::vector<uint8_t>& payload, uint32_t payloadBytes, Box3DCaseConfig* config);
int DecodeBox3DStep(const std::vector<uint8_t>& payload, uint32_t payloadBytes, int* stepCount);
}
