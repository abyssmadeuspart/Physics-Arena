#pragma once

#include "CoreMinimal.h"

namespace BenchmarkPolygonChaos
{

    constexpr int kPileXCount = 25;
    constexpr int kPileYCount = 16;
    constexpr int kPileZCount = 25;
    constexpr int kDynamicBodyCount = kPileXCount * kPileYCount * kPileZCount;
    constexpr int kStaticBodyCount = 5;
    constexpr int kBodyCount = kDynamicBodyCount + kStaticBodyCount;
    constexpr double kHalfExtent = 0.5;
    constexpr double kSpacing = 1.02;
    constexpr double kInitialY = 24.51;
    constexpr double kTimestep = 1.0 / 60.0;
    constexpr double kOpenContainerLateralEscape = 15.5;
    constexpr double kOpenContainerMaxY = 96.0;
    constexpr int kSolverPositionIterations = 4;
    constexpr int kSolverVelocityIterations = 1;
    constexpr int kSolverProjectionIterations = 1;
    constexpr const char* kEngineId = "unreal_chaos";
    constexpr const char* kEngineRef = "7deeb413d3dc1fc034f48d1aacc0861301829d32";
    constexpr const char* kCaseId = "box_container_pile_10k";
    constexpr const TCHAR* kCaseIdText = TEXT("box_container_pile_10k");
    constexpr const char* kFixtureSemantic = "open_container_falling_pile";
    constexpr const char* kFixtureVersion = "unreal_chaos_open_container_v1";

#if UE_BUILD_SHIPPING
    constexpr const char* kToolchainId = "unreal_chaos_ubt_win64_shipping";
#else
    constexpr const char* kToolchainId = "unreal_chaos_ubt_win64_development";
#endif

    enum class StepTimingMode : uint8
    {
        Untimed,
        Timed,
    };

    enum class ThreadExecutionMode : uint8
    {
        SingleThreaded,
        TaskGraphWorkers,
    };

    struct StaticBox
    {
        double PositionX;
        double PositionY;
        double PositionZ;
        double HalfExtentX;
        double HalfExtentY;
        double HalfExtentZ;
    };

    constexpr StaticBox kStaticBoxes[kStaticBodyCount] =
    {
        { 0.0, -0.5, 0.0, 14.5, 0.5, 14.5 },
        { -14.5, 11.75, 0.0, 0.5, 12.25, 14.5 },
        { 14.5, 11.75, 0.0, 0.5, 12.25, 14.5 },
        { 0.0, 11.75, -14.5, 14.5, 12.25, 0.5 },
        { 0.0, 11.75, 14.5, 14.5, 12.25, 0.5 },
    };

    int RequestedChaosWorkerCount(int ThreadCount);
    int RequestedTaskGraphWorkerCount(int ThreadCount);
    ThreadExecutionMode ThreadExecutionModeForThreadCount(int ThreadCount);

} // namespace BenchmarkPolygonChaos
