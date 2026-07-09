using UnrealBuildTool;
using System.IO;

public class BenchmarkPolygonChaosRunner : ModuleRules
{
    public BenchmarkPolygonChaosRunner(ReadOnlyTargetRules Target) : base(Target)
    {
        CppStandard = CppStandardVersion.Cpp20;
        PCHUsage = PCHUsageMode.NoPCHs;

        PublicIncludePathModuleNames.Add("Launch");
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "../../../rendering/include"));

        SetupModulePhysicsSupport(Target);

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "ApplicationCore",
                "Core",
                "CoreUObject",
                "Projects",
                "GeometryCore",
                "Sockets"
            }
        );

        PrivateDefinitions.Add("CHAOS_INCLUDE_LEVEL_1=1");
        PrivateDefinitions.Add("CSV_PROFILER=0");
    }
}
