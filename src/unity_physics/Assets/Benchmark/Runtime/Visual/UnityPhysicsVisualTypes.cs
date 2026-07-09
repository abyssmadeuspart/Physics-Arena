namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
    public struct SharedVisualCli
    {
        public string SharedVisualMode;
        public string ConnectHost;
        public string RunToken;
        public string EngineId;
        public string CaseId;
        public int ConnectPort;
        public int ThreadCount;
        public int RepeatIndex;
        public int StepCount;
        public int WarmupSteps;
    }

    public struct VisualRunConfig
    {
        public int ThreadCount;
        public int RepeatIndex;
        public int StepCount;
        public int WarmupSteps;
    }

    public struct VisualFrame
    {
        public ushort FrameType;
        public uint Sequence;
        public byte[] Payload;
    }

    public readonly struct UnityPhysicsCaseDescriptor
    {
        public readonly string EngineId;
        public readonly string CaseId;
        public readonly string FixtureSemantic;
        public readonly string FixtureVersion;
        public readonly int DynamicBodyCount;
        public readonly int StaticBodyCount;
        public readonly int BodyCount;
        public readonly float DynamicHalfExtent;

        public UnityPhysicsCaseDescriptor(
            string engineId,
            string caseId,
            string fixtureSemantic,
            string fixtureVersion,
            int dynamicBodyCount,
            int staticBodyCount,
            int bodyCount,
            float dynamicHalfExtent)
        {
            EngineId = engineId;
            CaseId = caseId;
            FixtureSemantic = fixtureSemantic;
            FixtureVersion = fixtureVersion;
            DynamicBodyCount = dynamicBodyCount;
            StaticBodyCount = staticBodyCount;
            BodyCount = bodyCount;
            DynamicHalfExtent = dynamicHalfExtent;
        }
    }

    public struct UnityPhysicsTransform
    {
        public float PositionX;
        public float PositionY;
        public float PositionZ;
        public float RotationX;
        public float RotationY;
        public float RotationZ;
        public float RotationW;
    }

    public struct UnityPhysicsStaticBox
    {
        public float PositionX;
        public float PositionY;
        public float PositionZ;
        public float HalfExtentX;
        public float HalfExtentY;
        public float HalfExtentZ;
    }
}
