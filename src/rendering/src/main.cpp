#include "benchmark_visual/visual_renderer.h"
#include "benchmark_visual/visual_transport.h"

#include <chrono>
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
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace benchmark_visual
{
#if defined(_WIN32)
using VisualSocket = SOCKET;
constexpr VisualSocket kInvalidVisualSocket = INVALID_SOCKET;
#else
using VisualSocket = int;
constexpr VisualSocket kInvalidVisualSocket = -1;
#endif

struct SharedVisualRendererCli
{
	const char* engineId;
	const char* caseId;
	const char* outputPath;
	const char* proofPath;
	const char* producerCommand;
	const char* producerLauncherCommand;
	std::vector<const char*> producerPrefixArgs;
	const char* visualMode;
	int threadCount;
	int repeatIndex;
	int stepCount;
	int warmupSteps;
};

struct VisualServer
{
	VisualSocket socket;
	uint16_t port;
};

struct VisualProducerProcess
{
#if defined(_WIN32)
	PROCESS_INFORMATION process;
#else
	pid_t pid;
#endif
};

int ParsePositiveInt(const char* value, int* parsed)
{
	int result = 0;
	for (int index = 0; value[index] != '\0'; ++index)
	{
		if (value[index] < '0' || value[index] > '9')
		{
			return RenderViewerStatus_InvalidArgument;
		}
		result = (result * 10) + (value[index] - '0');
		if (result > 1000000)
		{
			return RenderViewerStatus_InvalidArgument;
		}
	}
	if (result <= 0)
	{
		return RenderViewerStatus_InvalidArgument;
	}
	*parsed = result;
	return RenderViewerStatus_Ok;
}

int ParseNonNegativeInt(const char* value, int* parsed)
{
	int result = 0;
	for (int index = 0; value[index] != '\0'; ++index)
	{
		if (value[index] < '0' || value[index] > '9')
		{
			return RenderViewerStatus_InvalidArgument;
		}
		result = (result * 10) + (value[index] - '0');
		if (result > 1000000)
		{
			return RenderViewerStatus_InvalidArgument;
		}
	}
	*parsed = result;
	return RenderViewerStatus_Ok;
}

int ParseRendererCli(RenderViewerArgs args, SharedVisualRendererCli* cli)
{
	cli->engineId = "";
	cli->caseId = "";
	cli->outputPath = "";
	cli->proofPath = "";
	cli->producerCommand = "";
	cli->producerLauncherCommand = "";
	cli->visualMode = "live";
	cli->threadCount = 1;
	cli->repeatIndex = 0;
	cli->stepCount = 300;
	cli->warmupSteps = 30;

	for (int index = 1; index < args.argc; ++index)
	{
		const char* arg = args.argv[index];
		if (std::strncmp(arg, "--engine=", 9) == 0)
		{
			cli->engineId = arg + 9;
		}
		else if (std::strncmp(arg, "--case=", 7) == 0)
		{
			cli->caseId = arg + 7;
		}
		else if (std::strncmp(arg, "--output=", 9) == 0)
		{
			cli->outputPath = arg + 9;
		}
		else if (std::strncmp(arg, "--visual-proof-json=", 20) == 0)
		{
			cli->proofPath = arg + 20;
		}
		else if (std::strncmp(arg, "--producer-command=", 19) == 0)
		{
			cli->producerCommand = arg + 19;
		}
		else if (std::strncmp(arg, "--producer-launcher-command=", 28) == 0)
		{
			cli->producerLauncherCommand = arg + 28;
		}
		else if (std::strncmp(arg, "--producer-prefix-arg=", 22) == 0)
		{
			const char* value = arg + 22;
			if (value[0] == '\0')
			{
				std::fprintf(stderr, "invalid_argument name=producer-prefix-arg value=empty\n");
				return RenderViewerStatus_InvalidArgument;
			}
			cli->producerPrefixArgs.push_back(value);
		}
		else if (std::strncmp(arg, "--visual-mode=", 14) == 0)
		{
			cli->visualMode = arg + 14;
		}
		else if (std::strncmp(arg, "--thread-count=", 15) == 0)
		{
			if (ParsePositiveInt(arg + 15, &cli->threadCount) != RenderViewerStatus_Ok)
			{
				return RenderViewerStatus_InvalidArgument;
			}
		}
		else if (std::strncmp(arg, "--repeat-index=", 15) == 0)
		{
			if (ParseNonNegativeInt(arg + 15, &cli->repeatIndex) != RenderViewerStatus_Ok)
			{
				return RenderViewerStatus_InvalidArgument;
			}
		}
		else if (std::strncmp(arg, "--step-count=", 13) == 0)
		{
			if (ParsePositiveInt(arg + 13, &cli->stepCount) != RenderViewerStatus_Ok)
			{
				return RenderViewerStatus_InvalidArgument;
			}
		}
		else if (std::strncmp(arg, "--warmup-steps=", 15) == 0)
		{
			if (ParseNonNegativeInt(arg + 15, &cli->warmupSteps) != RenderViewerStatus_Ok)
			{
				return RenderViewerStatus_InvalidArgument;
			}
		}
		else
		{
			std::fprintf(stderr, "invalid_argument value=%s\n", arg);
			return RenderViewerStatus_InvalidArgument;
		}
	}

	if (cli->engineId[0] == '\0' || cli->caseId[0] == '\0' || cli->outputPath[0] == '\0' || cli->proofPath[0] == '\0' || cli->producerCommand[0] == '\0')
	{
		std::fprintf(stderr, "invalid_argument reason=missing_required_visual_cli_arg\n");
		return RenderViewerStatus_InvalidArgument;
	}
	if (std::strcmp(cli->visualMode, "live") != 0)
	{
		std::fprintf(stderr, "invalid_argument name=visual-mode value=%s\n", cli->visualMode);
		return RenderViewerStatus_InvalidArgument;
	}

	return RenderViewerStatus_Ok;
}

void CopyText(char* target, const char* source)
{
	std::snprintf(target, kVisualBridgeTextCapacity, "%s", source);
}

std::string QuoteProcessArgument(const char* value)
{
	std::string quoted = "\"";
	for (const char* cursor = value; *cursor != '\0'; ++cursor)
	{
		if (*cursor == '"')
		{
			quoted += '\\';
		}
		quoted += *cursor;
	}
	quoted += "\"";
	return quoted;
}

void AppendProcessArgument(std::string* commandLine, const char* value)
{
	if (!commandLine->empty())
	{
		*commandLine += " ";
	}
	*commandLine += QuoteProcessArgument(value);
}

int StartSocketSystem()
{
#if defined(_WIN32)
	WSADATA data = {};
	return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? RenderViewerStatus_Ok : RenderViewerStatus_RouteUnavailable;
#else
	return RenderViewerStatus_Ok;
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

int CreateLoopbackServer(VisualServer* server)
{
	server->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	server->port = 0u;
	if (server->socket == kInvalidVisualSocket)
	{
		return RenderViewerStatus_RouteUnavailable;
	}

	sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = 0;
	if (bind(server->socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
	{
		CloseVisualSocket(server->socket);
		server->socket = kInvalidVisualSocket;
		return RenderViewerStatus_RouteUnavailable;
	}
	if (listen(server->socket, 1) != 0)
	{
		CloseVisualSocket(server->socket);
		server->socket = kInvalidVisualSocket;
		return RenderViewerStatus_RouteUnavailable;
	}
	sockaddr_in boundAddress = {};
#if defined(_WIN32)
	int boundLength = sizeof(boundAddress);
#else
	socklen_t boundLength = sizeof(boundAddress);
#endif
	if (getsockname(server->socket, reinterpret_cast<sockaddr*>(&boundAddress), &boundLength) != 0)
	{
		CloseVisualSocket(server->socket);
		server->socket = kInvalidVisualSocket;
		return RenderViewerStatus_RouteUnavailable;
	}
	server->port = ntohs(boundAddress.sin_port);
	return RenderViewerStatus_Ok;
}

VisualSocket AcceptProducer(VisualServer server)
{
	fd_set readSet;
	FD_ZERO(&readSet);
	FD_SET(server.socket, &readSet);
	timeval timeout = {};
	timeout.tv_sec = 30;
	int ready = select(static_cast<int>(server.socket + 1), &readSet, nullptr, nullptr, &timeout);
	if (ready <= 0)
	{
		return kInvalidVisualSocket;
	}
	return accept(server.socket, nullptr, nullptr);
}

std::string MakeRunToken(const SharedVisualRendererCli& cli)
{
	auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	char buffer[128] = {};
	std::snprintf(buffer, sizeof(buffer), "%s-%s-%d-%d-%lld", cli.engineId, cli.caseId, cli.threadCount, cli.repeatIndex, static_cast<long long>(now));
	return buffer;
}

std::vector<std::string> ProducerSharedArguments(const SharedVisualRendererCli& cli, const std::string& portText, const std::string& token)
{
	std::vector<std::string> arguments;
	arguments.push_back("--producer-mode=shared-visual");
	arguments.push_back("--connect-host=127.0.0.1");
	arguments.push_back("--connect-port=" + portText);
	arguments.push_back("--run-token=" + token);
	arguments.push_back("--engine=" + std::string(cli.engineId));
	arguments.push_back("--case=" + std::string(cli.caseId));
	arguments.push_back("--thread-count=" + std::to_string(cli.threadCount));
	arguments.push_back("--repeat-index=" + std::to_string(cli.repeatIndex));
	arguments.push_back("--step-count=" + std::to_string(cli.stepCount));
	arguments.push_back("--warmup-steps=" + std::to_string(cli.warmupSteps));
	return arguments;
}

int LaunchProducerProcess(const SharedVisualRendererCli& cli, uint16_t port, const std::string& token, VisualProducerProcess* process)
{
	std::string portText = std::to_string(static_cast<unsigned int>(port));
	std::vector<std::string> sharedArguments = ProducerSharedArguments(cli, portText, token);
#if defined(_WIN32)
	std::string commandLine;
	if (cli.producerLauncherCommand[0] != '\0')
	{
		AppendProcessArgument(&commandLine, cli.producerLauncherCommand);
		AppendProcessArgument(&commandLine, cli.producerCommand);
	}
	else
	{
		AppendProcessArgument(&commandLine, cli.producerCommand);
	}
	for (const char* prefixArg : cli.producerPrefixArgs)
	{
		AppendProcessArgument(&commandLine, prefixArg);
	}
	for (const std::string& argument : sharedArguments)
	{
		AppendProcessArgument(&commandLine, argument.c_str());
	}
	STARTUPINFOA startup = {};
	startup.cb = sizeof(startup);
	std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
	mutableCommand.push_back('\0');
	if (CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process->process) == 0)
	{
		return RenderViewerStatus_RouteUnavailable;
	}
	return RenderViewerStatus_Ok;
#else
	pid_t pid = fork();
	if (pid < 0)
	{
		return RenderViewerStatus_RouteUnavailable;
	}
	if (pid == 0)
	{
		std::vector<std::string> processArgumentStorage;
		if (cli.producerLauncherCommand[0] != '\0')
		{
			processArgumentStorage.push_back(cli.producerLauncherCommand);
			processArgumentStorage.push_back(cli.producerCommand);
		}
		else
		{
			processArgumentStorage.push_back(cli.producerCommand);
		}
		for (const char* prefixArg : cli.producerPrefixArgs)
		{
			processArgumentStorage.push_back(prefixArg);
		}
		for (const std::string& argument : sharedArguments)
		{
			processArgumentStorage.push_back(argument);
		}
		std::vector<char*> processArguments;
		for (std::string& argument : processArgumentStorage)
		{
			processArguments.push_back(const_cast<char*>(argument.c_str()));
		}
		processArguments.push_back(nullptr);
		execv(processArgumentStorage[0].c_str(), processArguments.data());
		_exit(127);
	}
	process->pid = pid;
	return RenderViewerStatus_Ok;
#endif
}

void WaitForProducerProcess(VisualProducerProcess* process)
{
#if defined(_WIN32)
	if (process->process.hProcess != nullptr)
	{
		WaitForSingleObject(process->process.hProcess, 10000);
		CloseHandle(process->process.hThread);
		CloseHandle(process->process.hProcess);
		process->process = {};
	}
#else
	if (process->pid > 0)
	{
		int status = 0;
		waitpid(process->pid, &status, 0);
		process->pid = 0;
	}
#endif
}

int SendAll(VisualSocket socket, const uint8_t* data, uint32_t byteCount)
{
	uint32_t sent = 0u;
	while (sent < byteCount)
	{
		int result = send(socket, reinterpret_cast<const char*>(data + sent), static_cast<int>(byteCount - sent), 0);
		if (result <= 0)
		{
			return RenderViewerStatus_RouteUnavailable;
		}
		sent += static_cast<uint32_t>(result);
	}
	return RenderViewerStatus_Ok;
}

int ReceiveAll(VisualSocket socket, uint8_t* data, uint32_t byteCount)
{
	uint32_t received = 0u;
	while (received < byteCount)
	{
		int result = recv(socket, reinterpret_cast<char*>(data + received), static_cast<int>(byteCount - received), 0);
		if (result <= 0)
		{
			return RenderViewerStatus_RouteUnavailable;
		}
		received += static_cast<uint32_t>(result);
	}
	return RenderViewerStatus_Ok;
}

int SendFrame(VisualSocket socket, VisualFrameType frameType, uint32_t sequence, const void* payload, uint32_t payloadBytes)
{
	VisualFrameHeader header = {};
	header.magic = kVisualTransportMagic;
	header.version = kVisualTransportVersion;
	header.frameType = static_cast<uint16_t>(frameType);
	header.sequence = sequence;
	header.payloadBytes = payloadBytes;
	header.headerCrc = VisualTransportHeaderCrc(header);
	int status = SendAll(socket, reinterpret_cast<const uint8_t*>(&header), sizeof(header));
	if (status != RenderViewerStatus_Ok || payloadBytes == 0u)
	{
		return status;
	}
	return SendAll(socket, static_cast<const uint8_t*>(payload), payloadBytes);
}

int ReceiveFrame(VisualSocket socket, VisualFrameType expectedType, uint32_t expectedSequence, std::vector<uint8_t>& payload, uint32_t* payloadBytes)
{
	VisualFrameHeader header = {};
	int status = ReceiveAll(socket, reinterpret_cast<uint8_t*>(&header), sizeof(header));
	if (status != RenderViewerStatus_Ok)
	{
		return status;
	}
	if (header.magic != kVisualTransportMagic || header.version != kVisualTransportVersion ||
		header.frameType != static_cast<uint16_t>(expectedType) || header.sequence != expectedSequence ||
		header.payloadBytes > kMaxFramePayloadBytes || header.headerCrc != VisualTransportHeaderCrc(header))
	{
		return RenderViewerStatus_RouteUnavailable;
	}
	if (header.payloadBytes > payload.size())
	{
		return RenderViewerStatus_RouteUnavailable;
	}
	if (header.payloadBytes > 0u)
	{
		status = ReceiveAll(socket, payload.data(), header.payloadBytes);
		if (status != RenderViewerStatus_Ok)
		{
			return status;
		}
	}
	*payloadBytes = header.payloadBytes;
	return RenderViewerStatus_Ok;
}

int DecodeCompleted(const std::vector<uint8_t>& payload, uint32_t payloadBytes)
{
	if (payloadBytes != sizeof(VisualCompletedPayload))
	{
		return RenderViewerStatus_RouteUnavailable;
	}
	VisualCompletedPayload completed = {};
	std::memcpy(&completed, payload.data(), sizeof(completed));
	return completed.status == VisualTransportStatus_Ok ? RenderViewerStatus_Ok : RenderViewerStatus_RouteUnavailable;
}

int DecodeHelloAck(const SharedVisualRendererCli& cli, const std::string& token, const std::vector<uint8_t>& payload, uint32_t payloadBytes)
{
	if (payloadBytes != sizeof(VisualHelloAckPayload))
	{
		return RenderViewerStatus_RouteUnavailable;
	}
	VisualHelloAckPayload ack = {};
	std::memcpy(&ack, payload.data(), sizeof(ack));
	if (ack.status != VisualTransportStatus_Ok || ack.runTokenHash != VisualTransportHashText(token.c_str()))
	{
		return RenderViewerStatus_RouteUnavailable;
	}
	if (std::strcmp(ack.engineId, cli.engineId) != 0 || std::strcmp(ack.caseId, cli.caseId) != 0)
	{
		return RenderViewerStatus_RouteUnavailable;
	}
	return RenderViewerStatus_Ok;
}

int DecodeSnapshot(const std::vector<uint8_t>& payload, uint32_t payloadBytes, VisualTransform* transforms, VisualStaticBox* staticBoxes, VisualSnapshot* snapshot)
{
	if (payloadBytes < sizeof(VisualSnapshotPayloadHeader))
	{
		return RenderViewerStatus_RouteUnavailable;
	}
	VisualSnapshotPayloadHeader payloadHeader = {};
	std::memcpy(&payloadHeader, payload.data(), sizeof(payloadHeader));
	if (payloadHeader.status != VisualTransportStatus_Ok || payloadHeader.transformCount > kMaxTransforms || payloadHeader.staticBoxCount > kMaxStaticBoxes)
	{
		return RenderViewerStatus_RouteUnavailable;
	}
	uint32_t expectedPayloadBytes = static_cast<uint32_t>(sizeof(VisualSnapshotPayloadHeader)) +
		(payloadHeader.transformCount * static_cast<uint32_t>(sizeof(VisualTransform))) +
		(payloadHeader.staticBoxCount * static_cast<uint32_t>(sizeof(VisualStaticBox)));
	if (payloadBytes != expectedPayloadBytes)
	{
		return RenderViewerStatus_RouteUnavailable;
	}
	const uint8_t* cursor = payload.data() + sizeof(VisualSnapshotPayloadHeader);
	std::memcpy(transforms, cursor, payloadHeader.transformCount * sizeof(VisualTransform));
	cursor += payloadHeader.transformCount * sizeof(VisualTransform);
	std::memcpy(staticBoxes, cursor, payloadHeader.staticBoxCount * sizeof(VisualStaticBox));

	CopyText(snapshot->identity.caseId, payloadHeader.identity.caseId);
	CopyText(snapshot->identity.engineId, payloadHeader.identity.engineId);
	CopyText(snapshot->identity.fixtureSemantic, payloadHeader.identity.fixtureSemantic);
	CopyText(snapshot->identity.fixtureVersion, payloadHeader.identity.fixtureVersion);
	snapshot->identity.threadCount = static_cast<int>(payloadHeader.identity.threadCount);
	snapshot->identity.repeatIndex = static_cast<int>(payloadHeader.identity.repeatIndex);
	snapshot->identity.stepCount = static_cast<int>(payloadHeader.identity.stepCount);
	snapshot->identity.warmupSteps = static_cast<int>(payloadHeader.identity.warmupSteps);
	snapshot->identity.bodyCount = static_cast<int>(payloadHeader.identity.bodyCount);
	snapshot->identity.shapeCount = static_cast<int>(payloadHeader.identity.shapeCount);
	snapshot->identity.staticBoxCount = static_cast<int>(payloadHeader.identity.staticBoxCount);
	snapshot->transforms = transforms;
	snapshot->transformCapacity = static_cast<int>(kMaxTransforms);
	snapshot->transformCount = static_cast<int>(payloadHeader.transformCount);
	snapshot->staticBoxes = staticBoxes;
	snapshot->staticBoxCapacity = static_cast<int>(kMaxStaticBoxes);
	snapshot->staticBoxCount = static_cast<int>(payloadHeader.staticBoxCount);
	snapshot->dynamicHalfExtentX = payloadHeader.dynamicHalfExtentX;
	snapshot->dynamicHalfExtentY = payloadHeader.dynamicHalfExtentY;
	snapshot->dynamicHalfExtentZ = payloadHeader.dynamicHalfExtentZ;
	snapshot->stepIndex = static_cast<int>(payloadHeader.completedStepCount);
	snapshot->completedStepCount = static_cast<int>(payloadHeader.completedStepCount);
	snapshot->physicsElapsedMs = payloadHeader.timing.physicsElapsedMs;
	snapshot->timing = payloadHeader.timing;
	snapshot->renderer.rendererStatus = VisualRendererStatus_FrameSubmitted;
	snapshot->status = VisualBridgeStatus_Ok;
	return RenderViewerStatus_Ok;
}

int WriteVisualRawRow(const SharedVisualRendererCli& cli, const VisualSnapshot& snapshot, const char* proofPath, double renderElapsedMs)
{
	FILE* file = std::fopen(cli.outputPath, "w");
	if (file == nullptr)
	{
		std::fprintf(stderr, "result_failed reason=open_output path=%s\n", cli.outputPath);
		return RenderViewerStatus_InvalidArgument;
	}
	std::fprintf(file,
		"body_count,shape_count,invalid_transform_count,below_floor_count,out_of_bounds_count,case_status,metric_status,completed_step_count,physics_elapsed_ms,render_elapsed_ms,present_wait_ms,visual_validation_status,proof_path\n");
	std::fprintf(file,
		"%d,%d,0,0,0,ok,ok,%d,%.9f,%.9f,0.000000000,ok,%s\n",
		snapshot.identity.bodyCount,
		snapshot.identity.shapeCount,
		snapshot.completedStepCount,
		snapshot.physicsElapsedMs,
		renderElapsedMs,
		proofPath);
	std::fclose(file);
	return RenderViewerStatus_Ok;
}

int WriteVisualProofJson(const SharedVisualRendererCli& cli, const VisualSnapshot& snapshot, double renderElapsedMs)
{
	FILE* file = std::fopen(cli.proofPath, "w");
	if (file == nullptr)
	{
		std::fprintf(stderr, "result_failed reason=open_visual_proof_json path=%s\n", cli.proofPath);
		return RenderViewerStatus_InvalidArgument;
	}
	double msPerStep = snapshot.completedStepCount > 0 ? snapshot.physicsElapsedMs / static_cast<double>(snapshot.completedStepCount) : 0.0;
	std::fprintf(file,
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"run_status\": \"ok\",\n"
		"  \"visual_validation_status\": \"ok\",\n"
		"  \"completed_step_count\": %d,\n"
		"  \"body_count\": %d,\n"
		"  \"shape_count\": %d,\n"
		"  \"physics_elapsed_ms\": %.9f,\n"
		"  \"transport_elapsed_ms\": %.9f,\n"
		"  \"producer_wait_ms\": %.9f,\n"
		"  \"render_elapsed_ms\": %.9f,\n"
		"  \"present_wait_ms\": 0.000000000,\n"
		"  \"ms_per_step\": %.9f\n"
		"}\n",
		snapshot.completedStepCount,
		snapshot.identity.bodyCount,
		snapshot.identity.shapeCount,
		snapshot.physicsElapsedMs,
		snapshot.timing.transportElapsedMs,
		snapshot.timing.producerWaitMs,
		renderElapsedMs,
		msPerStep);
	std::fclose(file);
	return RenderViewerStatus_Ok;
}

int RunTransportHandshake(VisualSocket socket, const SharedVisualRendererCli& cli, const std::string& token, std::vector<uint8_t>& payload)
{
	VisualHelloPayload hello = {};
	CopyText(hello.runToken, token.c_str());
	CopyText(hello.engineId, cli.engineId);
	CopyText(hello.caseId, cli.caseId);
	uint32_t payloadBytes = 0u;
	int status = SendFrame(socket, VisualFrameType_Hello, 1u, &hello, sizeof(hello));
	if (status != RenderViewerStatus_Ok)
	{
		return status;
	}
	status = ReceiveFrame(socket, VisualFrameType_HelloAck, 1u, payload, &payloadBytes);
	if (status != RenderViewerStatus_Ok)
	{
		return status;
	}
	status = DecodeHelloAck(cli, token, payload, payloadBytes);
	if (status != RenderViewerStatus_Ok)
	{
		return status;
	}
	VisualStartPayload start = {};
	CopyText(start.config.caseId, cli.caseId);
	CopyText(start.config.engineId, cli.engineId);
	start.config.threadCount = static_cast<uint32_t>(cli.threadCount);
	start.config.repeatIndex = static_cast<uint32_t>(cli.repeatIndex);
	start.config.stepCount = static_cast<uint32_t>(cli.stepCount);
	start.config.warmupSteps = static_cast<uint32_t>(cli.warmupSteps);
	status = SendFrame(socket, VisualFrameType_Start, 2u, &start, sizeof(start));
	if (status != RenderViewerStatus_Ok)
	{
		return status;
	}
	status = ReceiveFrame(socket, VisualFrameType_Started, 2u, payload, &payloadBytes);
	if (status != RenderViewerStatus_Ok)
	{
		return status;
	}
	status = DecodeCompleted(payload, payloadBytes);
	if (status != RenderViewerStatus_Ok)
	{
		return status;
	}
	VisualStepPayload warmup = {};
	warmup.commandStepCount = static_cast<uint32_t>(cli.warmupSteps);
	status = SendFrame(socket, VisualFrameType_Warmup, 3u, &warmup, sizeof(warmup));
	if (status != RenderViewerStatus_Ok)
	{
		return status;
	}
	status = ReceiveFrame(socket, VisualFrameType_Completed, 3u, payload, &payloadBytes);
	if (status != RenderViewerStatus_Ok)
	{
		return status;
	}
	return DecodeCompleted(payload, payloadBytes);
}

int Run(RenderViewerArgs args)
{
	SharedVisualRendererCli cli = {};
	int parseStatus = ParseRendererCli(args, &cli);
	if (parseStatus != RenderViewerStatus_Ok)
	{
		return parseStatus;
	}
	int socketStatus = StartSocketSystem();
	if (socketStatus != RenderViewerStatus_Ok)
	{
		return socketStatus;
	}
	VisualServer server = {};
	server.socket = kInvalidVisualSocket;
	int serverStatus = CreateLoopbackServer(&server);
	if (serverStatus != RenderViewerStatus_Ok)
	{
		StopSocketSystem();
		return serverStatus;
	}
	std::string token = MakeRunToken(cli);
	VisualProducerProcess producerProcess = {};
	int launchStatus = LaunchProducerProcess(cli, server.port, token, &producerProcess);
	if (launchStatus != RenderViewerStatus_Ok)
	{
		CloseVisualSocket(server.socket);
		StopSocketSystem();
		return launchStatus;
	}
	VisualSocket producerSocket = AcceptProducer(server);
	CloseVisualSocket(server.socket);
	if (producerSocket == kInvalidVisualSocket)
	{
		WaitForProducerProcess(&producerProcess);
		StopSocketSystem();
		return RenderViewerStatus_RouteUnavailable;
	}

	std::vector<uint8_t> framePayload(kMaxFramePayloadBytes);
	int transportStatus = RunTransportHandshake(producerSocket, cli, token, framePayload);
	if (transportStatus != RenderViewerStatus_Ok)
	{
		CloseVisualSocket(producerSocket);
		WaitForProducerProcess(&producerProcess);
		StopSocketSystem();
		return transportStatus;
	}

	RenderPlatformState platform = {};
	RenderWindowDesc windowDesc = { "Shared Visual Renderer", 1280, 720 };
	int platformStatus = PlatformSdl3Create(&platform, windowDesc);
	if (platformStatus != RenderViewerStatus_Ok)
	{
		CloseVisualSocket(producerSocket);
		WaitForProducerProcess(&producerProcess);
		StopSocketSystem();
		return platformStatus;
	}

	int rendererStatus = RendererBgfxCreate(platform);
	if (rendererStatus != RenderViewerStatus_Ok)
	{
		PlatformSdl3Destroy(&platform);
		CloseVisualSocket(producerSocket);
		WaitForProducerProcess(&producerProcess);
		StopSocketSystem();
		return rendererStatus;
	}

	VisualTransform transforms[kVisualBridgeMaxTransforms] = {};
	VisualStaticBox staticBoxes[kVisualBridgeMaxStaticBoxes] = {};
	VisualSnapshot snapshot = {};
	double renderElapsedMs = 0.0;
	int visualStatus = RenderViewerStatus_Ok;
	uint32_t sequence = 4u;
	for (int step = 0; step < cli.stepCount; ++step)
	{
		VisualStepPayload stepPayload = {};
		stepPayload.commandStepCount = 1u;
		visualStatus = SendFrame(producerSocket, VisualFrameType_Step, sequence, &stepPayload, sizeof(stepPayload));
		if (visualStatus != RenderViewerStatus_Ok)
		{
			break;
		}
		uint32_t payloadBytes = 0u;
		visualStatus = ReceiveFrame(producerSocket, VisualFrameType_Snapshot, sequence, framePayload, &payloadBytes);
		if (visualStatus != RenderViewerStatus_Ok)
		{
			break;
		}
		visualStatus = DecodeSnapshot(framePayload, payloadBytes, transforms, staticBoxes, &snapshot);
		if (visualStatus != RenderViewerStatus_Ok)
		{
			break;
		}
		visualStatus = ValidateSnapshot(snapshot);
		if (visualStatus != VisualBridgeStatus_Ok)
		{
			break;
		}

		auto renderStart = std::chrono::steady_clock::now();
		PlatformSdl3Poll(&platform);
		if (platform.windowEventStatus == RenderWindowEventStatus_CloseRequested)
		{
			visualStatus = RenderViewerStatus_WindowCloseRequested;
			break;
		}
		rendererStatus = RendererBgfxDrawSnapshot(platform, snapshot);
		auto renderEnd = std::chrono::steady_clock::now();
		renderElapsedMs += std::chrono::duration<double, std::milli>(renderEnd - renderStart).count();
		if (rendererStatus != RenderViewerStatus_Ok)
		{
			visualStatus = rendererStatus;
			break;
		}
		sequence += 1u;
	}

	VisualCompletedPayload shutdown = {};
	shutdown.status = VisualTransportStatus_Ok;
	shutdown.completedStepCount = static_cast<uint32_t>(snapshot.completedStepCount);
	shutdown.physicsElapsedMs = snapshot.physicsElapsedMs;
	SendFrame(producerSocket, VisualFrameType_Shutdown, sequence, &shutdown, sizeof(shutdown));
	uint32_t shutdownPayloadBytes = 0u;
	ReceiveFrame(producerSocket, VisualFrameType_Completed, sequence, framePayload, &shutdownPayloadBytes);

	RendererBgfxDestroy();
	PlatformSdl3Destroy(&platform);
	CloseVisualSocket(producerSocket);
	WaitForProducerProcess(&producerProcess);
	StopSocketSystem();

	if (visualStatus != RenderViewerStatus_Ok || snapshot.completedStepCount != cli.stepCount)
	{
		return RenderViewerStatus_RouteUnavailable;
	}
	if (WriteVisualProofJson(cli, snapshot, renderElapsedMs) != RenderViewerStatus_Ok)
	{
		return RenderViewerStatus_InvalidArgument;
	}
	if (WriteVisualRawRow(cli, snapshot, cli.proofPath, renderElapsedMs) != RenderViewerStatus_Ok)
	{
		return RenderViewerStatus_InvalidArgument;
	}
	return RenderViewerStatus_Ok;
}
}

int main(int argc, char** argv)
{
	benchmark_visual::RenderViewerArgs args = { argc, argv };
	return benchmark_visual::Run(args);
}
