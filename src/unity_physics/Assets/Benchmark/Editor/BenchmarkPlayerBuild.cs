using System;
using System.IO;
using System.Text;
using Unity.Burst;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEngine;

namespace Bas3D.BenchmarkPolygon.UnityPhysics.Editor
{
    public static class BenchmarkPlayerBuild
    {
        private const string BootstrapScene = "Assets/Benchmark/Bootstrap/BenchmarkBootstrap.unity";
        private const string BuildMetadataFile = "player-build-metadata.json";
        private const string Backend = "il2cpp";

        public static void BuildHeadlessPlayer()
        {
            string buildDirectory = ArgumentValue("-benchmarkBuildPath", ".build/unity_physics/player-il2cpp");
            Directory.CreateDirectory(buildDirectory);
            BuildTarget target = Target();
            BuildTargetGroup targetGroup = BuildPipeline.GetBuildTargetGroup(target);
            NamedBuildTarget namedTarget = NamedBuildTarget.FromBuildTargetGroup(targetGroup);
            StandaloneBuildSubtarget subtarget = StandaloneBuildSubtarget.Player;
            ConfigureProductionBackend(namedTarget, subtarget);

            BuildPlayerOptions options = new BuildPlayerOptions
            {
                scenes = new[] { BootstrapScene },
                locationPathName = PlayerPath(buildDirectory),
                target = target,
                subtarget = (int)subtarget,
                options = BuildOptions.None
            };

            BuildReport report = BuildPipeline.BuildPlayer(options);
            if (report.summary.result != BuildResult.Succeeded)
            {
                throw new InvalidOperationException("Unity DOTS Physics benchmark Player build failed: " + report.summary.result);
            }

            WriteBuildMetadata(buildDirectory, target, subtarget);
        }

        private static void ConfigureProductionBackend(NamedBuildTarget namedTarget, StandaloneBuildSubtarget subtarget)
        {
            PlayerSettings.SetScriptingBackend(namedTarget, ScriptingImplementation.IL2CPP);
            if (PlayerSettings.GetScriptingBackend(namedTarget) != ScriptingImplementation.IL2CPP)
            {
                throw new InvalidOperationException("Unity DOTS Physics benchmark Player build did not select IL2CPP.");
            }

            BurstCompiler.Options.EnableBurstCompilation = true;
            BurstCompiler.Options.EnableBurstSafetyChecks = false;
            BurstCompiler.Options.ForceEnableBurstSafetyChecks = false;
            EditorUserBuildSettings.standaloneBuildSubtarget = subtarget;
        }

        private static string ArgumentValue(string name, string fallback)
        {
            string[] args = Environment.GetCommandLineArgs();
            for (int index = 0; index < args.Length - 1; ++index)
            {
                if (args[index] == name)
                {
                    return args[index + 1];
                }
            }

            return fallback;
        }

        private static BuildTarget Target()
        {
#if UNITY_EDITOR_WIN
            return BuildTarget.StandaloneWindows64;
#elif UNITY_EDITOR_LINUX
            return BuildTarget.StandaloneLinux64;
#elif UNITY_EDITOR_OSX
            return BuildTarget.StandaloneOSX;
#else
            throw new PlatformNotSupportedException("Unsupported Unity editor platform for benchmark Player build.");
#endif
        }

        private static string PlayerPath(string buildDirectory)
        {
#if UNITY_EDITOR_WIN
            return Path.Combine(buildDirectory, "BenchmarkPolygonUnityPhysics.exe");
#elif UNITY_EDITOR_OSX
            return Path.Combine(buildDirectory, "BenchmarkPolygonUnityPhysics.app");
#else
            return Path.Combine(buildDirectory, "BenchmarkPolygonUnityPhysics");
#endif
        }

        private static void WriteBuildMetadata(string buildDirectory, BuildTarget target, StandaloneBuildSubtarget subtarget)
        {
            BuildMetadata metadata = new BuildMetadata
            {
                schema_version = 1,
                build_target = BuildTargetId(target, subtarget),
                backend = Backend,
                burst_enabled_state = BurstCompiler.Options.EnableBurstCompilation ? "enabled" : "disabled",
                safety_check_state = BurstCompiler.Options.EnableBurstSafetyChecks || BurstCompiler.Options.ForceEnableBurstSafetyChecks ? "enabled" : "disabled"
            };
            string path = Path.Combine(buildDirectory, BuildMetadataFile);
            File.WriteAllText(path, JsonUtility.ToJson(metadata, true) + "\n", new UTF8Encoding(false));
        }

        private static string BuildTargetId(BuildTarget target, StandaloneBuildSubtarget subtarget)
        {
            return target.ToString().ToLowerInvariant() + "_" + subtarget.ToString().ToLowerInvariant();
        }

        [Serializable]
        private sealed class BuildMetadata
        {
            public int schema_version;
            public string build_target;
            public string backend;
            public string burst_enabled_state;
            public string safety_check_state;
        }
    }
}
