using UnrealBuildTool;
using System.Collections.Generic;

[SupportedPlatforms("Win64")]
public class BenchmarkPolygonChaosRunnerTarget : TargetRules
{
	public BenchmarkPolygonChaosRunnerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Monolithic;
		LaunchModuleName = "BenchmarkPolygonChaosRunner";

		bBuildDeveloperTools = false;
		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = true;
		bHasExports = false;
		bUseLoggingInShipping = true;
		bIsBuildingConsoleApplication = true;

		GlobalDefinitions.Add("CHAOS_SERIALIZE_OUT=0");
		GlobalDefinitions.Add("CSV_PROFILER=0");
	}
}
