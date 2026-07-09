namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public enum BepuCaseSlot
{
    BoxContainerPile10K = 1
}

public readonly struct BepuCaseDescriptor
{
    public readonly BepuCaseSlot Slot;
    public readonly string EngineId;
    public readonly string CaseId;
    public readonly string EngineRef;
    public readonly string ToolchainId;
    public readonly string FixtureSemantic;
    public readonly string FixtureVersion;
    public readonly int DynamicBodyCount;
    public readonly int BodyCount;
    public readonly int StaticBodyCount;
    public readonly float DynamicHalfExtent;

    public BepuCaseDescriptor(
        BepuCaseSlot slot,
        string engineId,
        string caseId,
        string engineRef,
        string toolchainId,
        string fixtureSemantic,
        string fixtureVersion,
        int dynamicBodyCount,
        int bodyCount,
        int staticBodyCount,
        float dynamicHalfExtent)
    {
        Slot = slot;
        EngineId = engineId;
        CaseId = caseId;
        EngineRef = engineRef;
        ToolchainId = toolchainId;
        FixtureSemantic = fixtureSemantic;
        FixtureVersion = fixtureVersion;
        DynamicBodyCount = dynamicBodyCount;
        BodyCount = bodyCount;
        StaticBodyCount = staticBodyCount;
        DynamicHalfExtent = dynamicHalfExtent;
    }
}
