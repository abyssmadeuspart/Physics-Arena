using System;
using System.IO;
using System.Text;
using Unity.Burst;
using Unity.CodeEditor;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEngine;

namespace Bas3D.BenchmarkPolygon.UnityPhysics.Editor
{
    public static class BenchmarkPlayerBuild
    {
        public const string BootstrapScene = "Assets/Benchmark/Bootstrap/BenchmarkBootstrap.unity";
        public const string BuildMetadataFile = "player-build-metadata.json";
        public const string Backend = "il2cpp";

        public static void GenerateProjectFiles()
        {
            string editorPath = ArgumentValue("-benchmarkIdePath", string.Empty);
            if (!File.Exists(editorPath))
            {
                throw new InvalidOperationException("Unity project-file generation requires the configured IDE path.");
            }
            string previousEditor = CodeEditor.CurrentEditorInstallation;
            try
            {
                CodeEditor.SetExternalScriptEditor(editorPath);
                CodeEditor.CurrentEditor.SyncAll();
            }
            finally
            {
                CodeEditor.SetExternalScriptEditor(previousEditor);
            }
        }

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
            if ((options.options & BuildOptions.Development) != 0)
            {
                throw new InvalidOperationException("Unity DOTS Physics benchmark Player build selected Development.");
            }

            string previousIl2CppArgs = PlayerSettings.GetAdditionalIl2CppArgs();
            try
            {
                if (target == BuildTarget.StandaloneWindows64)
                {
                    PlayerSettings.SetAdditionalIl2CppArgs(previousIl2CppArgs +
                        " --linker-flags=\"/PDBALTPATH:GameAssembly.pdb\"");
                }

                BuildReport report = BuildPipeline.BuildPlayer(options);
                if (report.summary.result != BuildResult.Succeeded)
                {
                    throw new InvalidOperationException("Unity DOTS Physics benchmark Player build failed: " + report.summary.result);
                }
            }
            finally
            {
                PlayerSettings.SetAdditionalIl2CppArgs(previousIl2CppArgs);
            }

            WriteBuildMetadata(buildDirectory, target, subtarget);
        }

        public static void ConfigureProductionBackend(NamedBuildTarget namedTarget, StandaloneBuildSubtarget subtarget)
        {
            PlayerSettings.SetScriptingBackend(namedTarget, ScriptingImplementation.IL2CPP);
            PlayerSettings.SetIl2CppCompilerConfiguration(namedTarget, Il2CppCompilerConfiguration.Release);
            PlayerSettings.SetIl2CppCodeGeneration(namedTarget, Il2CppCodeGeneration.OptimizeSpeed);
            if (PlayerSettings.GetScriptingBackend(namedTarget) != ScriptingImplementation.IL2CPP)
            {
                throw new InvalidOperationException("Unity DOTS Physics benchmark Player build did not select IL2CPP.");
            }
            if (PlayerSettings.GetIl2CppCompilerConfiguration(namedTarget) != Il2CppCompilerConfiguration.Release)
            {
                throw new InvalidOperationException(
                    "Unity DOTS Physics benchmark Player build did not select IL2CPP Release.");
            }
            if (PlayerSettings.GetIl2CppCodeGeneration(namedTarget) != Il2CppCodeGeneration.OptimizeSpeed)
            {
                throw new InvalidOperationException(
                    "Unity DOTS Physics benchmark Player build did not select IL2CPP OptimizeSpeed.");
            }

            BurstCompiler.Options.EnableBurstCompilation = true;
            BurstCompiler.Options.EnableBurstSafetyChecks = false;
            BurstCompiler.Options.ForceEnableBurstSafetyChecks = false;
            if (!BurstCompiler.Options.EnableBurstCompilation ||
                BurstCompiler.Options.EnableBurstSafetyChecks ||
                BurstCompiler.Options.ForceEnableBurstSafetyChecks)
            {
                throw new InvalidOperationException(
                    "Unity DOTS Physics benchmark Player build did not retain the production Burst profile.");
            }

            EditorUserBuildSettings.standaloneBuildSubtarget = subtarget;
        }

        public static string ArgumentValue(string name, string fallback)
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

        public static BuildTarget Target()
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

        public static string PlayerPath(string buildDirectory)
        {
#if UNITY_EDITOR_WIN
            return Path.Combine(buildDirectory, "BenchmarkPolygonUnityPhysics.exe");
#elif UNITY_EDITOR_OSX
            return Path.Combine(buildDirectory, "BenchmarkPolygonUnityPhysics.app");
#else
            return Path.Combine(buildDirectory, "BenchmarkPolygonUnityPhysics");
#endif
        }

        public static void WriteBuildMetadata(string buildDirectory, BuildTarget target, StandaloneBuildSubtarget subtarget)
        {
            BuildMetadata metadata = new BuildMetadata
            {
                schema_version = 1,
                build_target = BuildTargetId(target, subtarget),
                backend = Backend,
                development_build_state = "disabled",
                il2cpp_compiler_configuration = "release",
                il2cpp_code_generation = "optimize_speed",
                burst_enabled_state = BurstCompiler.Options.EnableBurstCompilation ? "enabled" : "disabled",
                safety_check_state = BurstCompiler.Options.EnableBurstSafetyChecks || BurstCompiler.Options.ForceEnableBurstSafetyChecks ? "enabled" : "disabled"
            };
            string path = Path.Combine(buildDirectory, BuildMetadataFile);
            File.WriteAllText(path, JsonUtility.ToJson(metadata, true) + "\n", new UTF8Encoding(false));
        }

        public static string BuildTargetId(BuildTarget target, StandaloneBuildSubtarget subtarget)
        {
            return target.ToString().ToLowerInvariant() + "_" + subtarget.ToString().ToLowerInvariant();
        }

        [Serializable]
        public struct BuildMetadata
        {
            public int schema_version;
            public string build_target;
            public string backend;
            public string development_build_state;
            public string il2cpp_compiler_configuration;
            public string il2cpp_code_generation;
            public string burst_enabled_state;
            public string safety_check_state;
        }
    }
}
