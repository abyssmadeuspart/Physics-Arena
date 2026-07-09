#include "Visual/BenchmarkPolygonChaosSharedVisualMode.h"

#include "Cases/BenchmarkPolygonChaosCase.h"

#include "Async/TaskGraphInterfaces.h"
#include "Containers/StringConv.h"
#include "HAL/PlatformProcess.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "benchmark_visual/visual_transport.h"

#include <cstdio>

namespace BenchmarkPolygonChaos
{

    struct SharedVisualCli
    {
        const TCHAR* SharedVisualMode = TEXT("");
        const TCHAR* ConnectHost = TEXT("127.0.0.1");
        const TCHAR* RunToken = TEXT("");
        const TCHAR* EngineId = TEXT("");
        const TCHAR* CaseId = kCaseIdText;
        int ConnectPort = 0;
        int ThreadCount = 1;
        int RepeatIndex = 0;
        int StepCount = 300;
        int WarmupSteps = 0;
    };

    struct VisualConnection
    {
        ISocketSubsystem* SocketSubsystem = nullptr;
        FSocket* Socket = nullptr;
    };

    struct IncomingVisualFrame
    {
        uint16 FrameType = 0;
        uint32 Sequence = 0;
        TArray<uint8> Payload;
    };

    int ParseSharedVisualInt(const TCHAR* Name, const TCHAR* Value, int Minimum, int* Parsed)
    {
        TCHAR* End = nullptr;
        const int64 Result = FCString::Strtoi64(Value, &End, 10);
        if (End == Value || *End != TEXT('\0') || Result < Minimum || Result > 1000000)
        {
            std::fprintf(stderr, "invalid_argument name=%s value=%s\n", TCHAR_TO_UTF8(Name), TCHAR_TO_UTF8(Value));
            return 2;
        }

        *Parsed = static_cast<int>(Result);
        return 0;
    }

    int ParseSharedVisualArgs(const CliOptions& Options, SharedVisualCli* Cli)
    {
        Cli->SharedVisualMode = OptionValue(Options, TEXT("--producer-mode"), TEXT(""));
        Cli->ConnectHost = OptionValue(Options, TEXT("--connect-host"), TEXT("127.0.0.1"));
        Cli->RunToken = OptionValue(Options, TEXT("--run-token"), TEXT(""));
        Cli->EngineId = OptionValue(Options, TEXT("--engine"), TEXT(""));
        Cli->CaseId = OptionValue(Options, TEXT("--case"), kCaseIdText);
        if (ParseSharedVisualInt(TEXT("connect-port"),
                                 OptionValue(Options, TEXT("--connect-port"), TEXT("0")),
                                 1,
                                 &Cli->ConnectPort) != 0 ||
            ParseSharedVisualInt(TEXT("thread-count"),
                                 OptionValue(Options, TEXT("--thread-count"), TEXT("1")),
                                 1,
                                 &Cli->ThreadCount) != 0 ||
            ParseSharedVisualInt(TEXT("repeat-index"),
                                 OptionValue(Options, TEXT("--repeat-index"), TEXT("0")),
                                 0,
                                 &Cli->RepeatIndex) != 0 ||
            ParseSharedVisualInt(TEXT("step-count"),
                                 OptionValue(Options, TEXT("--step-count"), TEXT("300")),
                                 1,
                                 &Cli->StepCount) != 0 ||
            ParseSharedVisualInt(TEXT("warmup-steps"),
                                 OptionValue(Options, TEXT("--warmup-steps"), TEXT("0")),
                                 0,
                                 &Cli->WarmupSteps) != 0)
        {
            return 2;
        }

        if (FCString::Strcmp(Cli->SharedVisualMode, TEXT("shared-visual")) != 0 ||
            FCString::Strcmp(Cli->ConnectHost, TEXT("127.0.0.1")) != 0 ||
            FCString::Strcmp(Cli->EngineId, TEXT("unreal_chaos")) != 0 ||
            FCString::Strcmp(Cli->CaseId, kCaseIdText) != 0 ||
            Cli->RunToken[0] == TEXT('\0') ||
            Cli->ConnectPort <= 0)
        {
            std::fprintf(stderr, "invalid_argument reason=shared_visual_mode_contract\n");
            return 2;
        }

        return 0;
    }

    void CopyVisualText(char* Target, const char* Source)
    {
        FCStringAnsi::Strncpy(Target, Source, benchmark_visual::kTextCapacity);
        Target[benchmark_visual::kTextCapacity - 1u] = '\0';
    }

    int ConnectRenderer(const SharedVisualCli& Cli, VisualConnection* Connection)
    {
        Connection->SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        if (Connection->SocketSubsystem == nullptr)
        {
            return 2;
        }

        Connection->Socket = Connection->SocketSubsystem->CreateSocket(NAME_Stream, TEXT("BenchmarkPolygonChaosVisual"), false);
        if (Connection->Socket == nullptr)
        {
            return 2;
        }

        TSharedRef<FInternetAddr> Address = Connection->SocketSubsystem->CreateInternetAddr();
        bool AddressStatus = false;
        Address->SetIp(Cli.ConnectHost, AddressStatus);
        Address->SetPort(Cli.ConnectPort);
        if (!AddressStatus || !Connection->Socket->Connect(*Address))
        {
            return 2;
        }

        return 0;
    }

    void CloseRendererConnection(VisualConnection* Connection)
    {
        if (Connection->Socket != nullptr)
        {
            Connection->Socket->Close();
            if (Connection->SocketSubsystem != nullptr)
            {
                Connection->SocketSubsystem->DestroySocket(Connection->Socket);
            }
            Connection->Socket = nullptr;
        }
    }

    int SendAll(FSocket* Socket, const uint8* Data, int32 ByteCount)
    {
        int32 Sent = 0;
        while (Sent < ByteCount)
        {
            int32 BatchSent = 0;
            if (!Socket->Send(Data + Sent, ByteCount - Sent, BatchSent) || BatchSent <= 0)
            {
                return 2;
            }
            Sent += BatchSent;
        }
        return 0;
    }

    int ReceiveAll(FSocket* Socket, uint8* Data, int32 ByteCount)
    {
        int32 Received = 0;
        while (Received < ByteCount)
        {
            int32 BatchReceived = 0;
            if (!Socket->Recv(Data + Received, ByteCount - Received, BatchReceived) || BatchReceived <= 0)
            {
                return 2;
            }
            Received += BatchReceived;
        }
        return 0;
    }

    uint32 PayloadBytes(const TArray<uint8>& Payload)
    {
        return static_cast<uint32>(Payload.Num());
    }

    int ReceiveAnyFrame(FSocket* Socket, uint32 Sequence, IncomingVisualFrame* Frame)
    {
        benchmark_visual::VisualFrameHeader Header = {};
        if (ReceiveAll(Socket, reinterpret_cast<uint8*>(&Header), sizeof(Header)) != 0)
        {
            return 2;
        }

        if (Header.magic != benchmark_visual::kVisualTransportMagic ||
            Header.version != benchmark_visual::kVisualTransportVersion ||
            Header.sequence != Sequence ||
            Header.payloadBytes > benchmark_visual::kMaxFramePayloadBytes ||
            Header.headerCrc != benchmark_visual::VisualTransportHeaderCrc(Header))
        {
            std::fprintf(stderr,
                         "run_failed reason=invalid_frame_header sequence=%u frame_type=%u payload_bytes=%u\n",
                         Sequence,
                         static_cast<unsigned int>(Header.frameType),
                         static_cast<unsigned int>(Header.payloadBytes));
            return 2;
        }

        Frame->FrameType = Header.frameType;
        Frame->Sequence = Header.sequence;
        Frame->Payload.SetNumUninitialized(static_cast<int32>(Header.payloadBytes));
        if (Header.payloadBytes > 0u &&
            ReceiveAll(Socket, Frame->Payload.GetData(), Frame->Payload.Num()) != 0)
        {
            return 2;
        }

        return 0;
    }

    int ReceiveFrame(FSocket* Socket, uint16 FrameType, uint32 Sequence, IncomingVisualFrame* Frame)
    {
        if (ReceiveAnyFrame(Socket, Sequence, Frame) != 0)
        {
            return 2;
        }
        if (Frame->FrameType != FrameType)
        {
            std::fprintf(stderr,
                         "run_failed reason=unexpected_frame expected=%u actual=%u sequence=%u\n",
                         static_cast<unsigned int>(FrameType),
                         static_cast<unsigned int>(Frame->FrameType),
                         Sequence);
            return 2;
        }

        return 0;
    }

    int SendFrame(FSocket* Socket, uint16 FrameType, uint32 Sequence, const void* Payload, uint32 ByteCount)
    {
        benchmark_visual::VisualFrameHeader Header = {};
        Header.magic = benchmark_visual::kVisualTransportMagic;
        Header.version = benchmark_visual::kVisualTransportVersion;
        Header.frameType = FrameType;
        Header.sequence = Sequence;
        Header.payloadBytes = ByteCount;
        Header.headerCrc = benchmark_visual::VisualTransportHeaderCrc(Header);
        if (SendAll(Socket, reinterpret_cast<const uint8*>(&Header), sizeof(Header)) != 0)
        {
            return 2;
        }
        if (ByteCount > 0u && SendAll(Socket, static_cast<const uint8*>(Payload), static_cast<int32>(ByteCount)) != 0)
        {
            return 2;
        }
        return 0;
    }

    int SendCompleted(FSocket* Socket,
                      uint16 FrameType,
                      uint32 Sequence,
                      uint32 Status,
                      uint32 CompletedStepCount,
                      double PhysicsElapsedMs)
    {
        benchmark_visual::VisualCompletedPayload Payload = {};
        Payload.status = Status;
        Payload.completedStepCount = CompletedStepCount;
        Payload.physicsElapsedMs = PhysicsElapsedMs;
        return SendFrame(Socket, FrameType, Sequence, &Payload, sizeof(Payload));
    }

    int DecodeHello(const SharedVisualCli& Cli, const TArray<uint8>& Payload)
    {
        if (PayloadBytes(Payload) != sizeof(benchmark_visual::VisualHelloPayload))
        {
            return 2;
        }

        const benchmark_visual::VisualHelloPayload* Hello =
            reinterpret_cast<const benchmark_visual::VisualHelloPayload*>(Payload.GetData());
        FTCHARToUTF8 RunToken(Cli.RunToken);
        if (FCStringAnsi::Strcmp(Hello->runToken, RunToken.Get()) != 0 ||
            FCStringAnsi::Strcmp(Hello->engineId, kEngineId) != 0 ||
            FCStringAnsi::Strcmp(Hello->caseId, kCaseId) != 0)
        {
            return 2;
        }
        return 0;
    }

    int DecodeStart(const TArray<uint8>& Payload, benchmark_visual::VisualRunConfig* Config)
    {
        if (PayloadBytes(Payload) != sizeof(benchmark_visual::VisualStartPayload))
        {
            return 2;
        }

        const benchmark_visual::VisualStartPayload* Start =
            reinterpret_cast<const benchmark_visual::VisualStartPayload*>(Payload.GetData());
        if (FCStringAnsi::Strcmp(Start->config.engineId, kEngineId) != 0 ||
            FCStringAnsi::Strcmp(Start->config.caseId, kCaseId) != 0)
        {
            return 2;
        }

        *Config = Start->config;
        return 0;
    }

    int DecodeStep(const TArray<uint8>& Payload, int* StepCount)
    {
        if (PayloadBytes(Payload) != sizeof(benchmark_visual::VisualStepPayload))
        {
            return 2;
        }

        const benchmark_visual::VisualStepPayload* Step =
            reinterpret_cast<const benchmark_visual::VisualStepPayload*>(Payload.GetData());
        *StepCount = static_cast<int>(Step->commandStepCount);
        return *StepCount > 0 ? 0 : 2;
    }

    int SendHelloAck(FSocket* Socket, const SharedVisualCli& Cli, uint32 Sequence)
    {
        benchmark_visual::VisualHelloAckPayload Payload = {};
        CopyVisualText(Payload.engineId, kEngineId);
        CopyVisualText(Payload.caseId, kCaseId);
        CopyVisualText(Payload.producerBuildId, "unreal_chaos_polygon_runner");
        Payload.processId = static_cast<uint32>(FPlatformProcess::GetCurrentProcessId());
        Payload.supportedFrameFlags = 0u;
        FTCHARToUTF8 RunToken(Cli.RunToken);
        Payload.runTokenHash = benchmark_visual::VisualTransportHashText(RunToken.Get());
        Payload.status = benchmark_visual::VisualTransportStatus_Ok;
        return SendFrame(Socket, benchmark_visual::VisualFrameType_HelloAck, Sequence, &Payload, sizeof(Payload));
    }

    void WriteChaosSnapshotPayload(const benchmark_visual::VisualRunConfig& Config,
                                   const ChaosCaseState& State,
                                   TArray<uint8>* Payload)
    {
        const int32 HeaderBytes = sizeof(benchmark_visual::VisualSnapshotPayloadHeader);
        const int32 TransformBytes = kDynamicBodyCount * sizeof(benchmark_visual::VisualTransform);
        const int32 StaticBoxBytes = kStaticBodyCount * sizeof(benchmark_visual::VisualStaticBox);
        Payload->SetNumUninitialized(HeaderBytes + TransformBytes + StaticBoxBytes);

        benchmark_visual::VisualSnapshotPayloadHeader Header = {};
        CopyVisualText(Header.identity.caseId, kCaseId);
        CopyVisualText(Header.identity.engineId, kEngineId);
        CopyVisualText(Header.identity.fixtureSemantic, kFixtureSemantic);
        CopyVisualText(Header.identity.fixtureVersion, kFixtureVersion);
        Header.identity.threadCount = Config.threadCount;
        Header.identity.repeatIndex = Config.repeatIndex;
        Header.identity.stepCount = Config.stepCount;
        Header.identity.warmupSteps = Config.warmupSteps;
        Header.identity.bodyCount = kBodyCount;
        Header.identity.shapeCount = kBodyCount;
        Header.identity.staticBoxCount = kStaticBodyCount;
        Header.timing.physicsElapsedMs = State.PhysicsElapsedMs;
        Header.transformCount = kDynamicBodyCount;
        Header.staticBoxCount = kStaticBodyCount;
        Header.completedStepCount = static_cast<uint32>(State.CompletedStepCount);
        Header.status = benchmark_visual::VisualTransportStatus_Ok;
        Header.dynamicHalfExtentX = static_cast<float>(kHalfExtent);
        Header.dynamicHalfExtentY = static_cast<float>(kHalfExtent);
        Header.dynamicHalfExtentZ = static_cast<float>(kHalfExtent);

        uint8* Cursor = Payload->GetData();
        FMemory::Memcpy(Cursor, &Header, sizeof(Header));
        Cursor += sizeof(Header);

        benchmark_visual::VisualTransform* Transforms =
            reinterpret_cast<benchmark_visual::VisualTransform*>(Cursor);
        for (int32 Index = 0; Index < State.DynamicBodies.Num(); ++Index)
        {
            Chaos::FPBDRigidParticleHandle* Particle = State.DynamicBodies[Index];
            const Chaos::FVec3 Position = Particle->GetX();
            const Chaos::FRotation3 Rotation = Particle->GetR();
            Transforms[Index].positionX = static_cast<float>(Position.X);
            Transforms[Index].positionY = static_cast<float>(Position.Y);
            Transforms[Index].positionZ = static_cast<float>(Position.Z);
            Transforms[Index].rotationX = static_cast<float>(Rotation.X);
            Transforms[Index].rotationY = static_cast<float>(Rotation.Y);
            Transforms[Index].rotationZ = static_cast<float>(Rotation.Z);
            Transforms[Index].rotationW = static_cast<float>(Rotation.W);
        }
        Cursor += TransformBytes;

        benchmark_visual::VisualStaticBox* StaticBoxes =
            reinterpret_cast<benchmark_visual::VisualStaticBox*>(Cursor);
        for (int32 Index = 0; Index < kStaticBodyCount; ++Index)
        {
            StaticBoxes[Index].positionX = static_cast<float>(kStaticBoxes[Index].PositionX);
            StaticBoxes[Index].positionY = static_cast<float>(kStaticBoxes[Index].PositionY);
            StaticBoxes[Index].positionZ = static_cast<float>(kStaticBoxes[Index].PositionZ);
            StaticBoxes[Index].halfExtentX = static_cast<float>(kStaticBoxes[Index].HalfExtentX);
            StaticBoxes[Index].halfExtentY = static_cast<float>(kStaticBoxes[Index].HalfExtentY);
            StaticBoxes[Index].halfExtentZ = static_cast<float>(kStaticBoxes[Index].HalfExtentZ);
        }
    }

    int RunSharedVisualLoop(const SharedVisualCli& Cli, const ThreadRuntimeState& RuntimeState)
    {
        VisualConnection Connection = {};
        if (ConnectRenderer(Cli, &Connection) != 0)
        {
            std::fprintf(stderr, "run_failed reason=connect\n");
            return 2;
        }

        int Status = 0;
        IncomingVisualFrame Frame = {};
        benchmark_visual::VisualRunConfig Config = {};
        TArray<uint8> SnapshotPayload;
        ChaosCaseState State(RuntimeState);
        do
        {
            if (ReceiveFrame(Connection.Socket, benchmark_visual::VisualFrameType_Hello, 1u, &Frame) != 0 ||
                DecodeHello(Cli, Frame.Payload) != 0 ||
                SendHelloAck(Connection.Socket, Cli, 1u) != 0 ||
                ReceiveFrame(Connection.Socket, benchmark_visual::VisualFrameType_Start, 2u, &Frame) != 0 ||
                DecodeStart(Frame.Payload, &Config) != 0)
            {
                Status = 2;
                break;
            }

            if (CreateChaosCaseState(&State) != 0 ||
                SendCompleted(Connection.Socket,
                              benchmark_visual::VisualFrameType_Started,
                              2u,
                              benchmark_visual::VisualTransportStatus_Ok,
                              0u,
                              0.0) != 0)
            {
                SendCompleted(Connection.Socket,
                              benchmark_visual::VisualFrameType_Started,
                              2u,
                              benchmark_visual::VisualTransportStatus_ProducerFailed,
                              0u,
                              0.0);
                Status = 2;
                break;
            }

            if (ReceiveFrame(Connection.Socket, benchmark_visual::VisualFrameType_Warmup, 3u, &Frame) != 0)
            {
                Status = 2;
                break;
            }
            int WarmupSteps = 0;
            if (DecodeStep(Frame.Payload, &WarmupSteps) != 0 ||
                RunWarmup(WarmupSteps, RuntimeState) != 0 ||
                SendCompleted(Connection.Socket,
                              benchmark_visual::VisualFrameType_Completed,
                              3u,
                              benchmark_visual::VisualTransportStatus_Ok,
                              0u,
                              0.0) != 0)
            {
                SendCompleted(Connection.Socket,
                              benchmark_visual::VisualFrameType_Completed,
                              3u,
                              benchmark_visual::VisualTransportStatus_ProducerFailed,
                              0u,
                              0.0);
                Status = 2;
                break;
            }

            for (uint32 Sequence = 4u;; ++Sequence)
            {
                if (ReceiveAnyFrame(Connection.Socket, Sequence, &Frame) != 0)
                {
                    Status = 2;
                    break;
                }
                if (Frame.FrameType == benchmark_visual::VisualFrameType_Shutdown)
                {
                    Status = SendCompleted(Connection.Socket,
                                           benchmark_visual::VisualFrameType_Completed,
                                           Sequence,
                                           benchmark_visual::VisualTransportStatus_Ok,
                                           static_cast<uint32>(State.CompletedStepCount),
                                           State.PhysicsElapsedMs);
                    break;
                }
                if (Frame.FrameType != benchmark_visual::VisualFrameType_Step)
                {
                    Status = 2;
                    break;
                }

                int StepCount = 0;
                if (DecodeStep(Frame.Payload, &StepCount) != 0)
                {
                    Status = 2;
                    break;
                }
                StepChaosCase(&State, StepCount, StepTimingMode::Timed);
                WriteChaosSnapshotPayload(Config, State, &SnapshotPayload);
                if (SendFrame(Connection.Socket,
                              benchmark_visual::VisualFrameType_Snapshot,
                              Sequence,
                              SnapshotPayload.GetData(),
                              static_cast<uint32>(SnapshotPayload.Num())) != 0)
                {
                    Status = 2;
                    break;
                }
            }
        } while (false);

        CloseRendererConnection(&Connection);
        return Status;
    }

    int RunSharedVisualMode(const CliOptions& Options)
    {
        SharedVisualCli Cli = {};
        if (ParseSharedVisualArgs(Options, &Cli) != 0)
        {
            return 2;
        }

        ScopedCoreRuntime CoreRuntime(Cli.ThreadCount);
        FTaskTagScope GameThreadScope(ETaskTag::EGameThread);
        return RunSharedVisualLoop(Cli, CoreRuntime.State);
    }

} // namespace BenchmarkPolygonChaos
