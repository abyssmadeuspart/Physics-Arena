using BepuPhysics;
using BepuUtilities;
using BepuUtilities.Memory;

namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public struct BepuCaseRegistry
{
    public static BepuCaseDescriptor DefaultCase()
    {
        return BoxContainerPileDescriptor();
    }

    public static int Resolve(string caseId, out BepuCaseDescriptor descriptor)
    {
        if (caseId == BepuBoxContainerPileCase.CaseId)
        {
            descriptor = BoxContainerPileDescriptor();
            return 0;
        }

        descriptor = default;
        Console.Error.WriteLine($"invalid_argument name=case value={caseId}");
        return 2;
    }

    public static BepuCaseDescriptor BoxContainerPileDescriptor()
    {
        return new BepuCaseDescriptor(
            BepuCaseSlot.BoxContainerPile10K,
            BepuBoxContainerPileCase.EngineId,
            BepuBoxContainerPileCase.CaseId,
            BepuBoxContainerPileCase.EngineRef,
            BepuBoxContainerPileCase.ToolchainId,
            BepuBoxContainerPileCase.FixtureSemantic,
            BepuBoxContainerPileCase.FixtureVersion,
            BepuBoxContainerPileCase.DynamicBodyCount,
            BepuBoxContainerPileCase.BodyCount,
            BepuBoxContainerPileCase.StaticBodyCount,
            BepuBoxContainerPileCase.HalfExtent);
    }

    public static Simulation CreateSimulation(
        in BepuCaseDescriptor descriptor,
        BufferPool bufferPool,
        int workerCount,
        out ThreadDispatcher threadDispatcher)
    {
        switch (descriptor.Slot)
        {
            case BepuCaseSlot.BoxContainerPile10K:
                return BepuBoxContainerPileCase.CreateSimulation(bufferPool, workerCount, out threadDispatcher);

            default:
                throw new InvalidOperationException($"Unsupported BEPU case slot: {descriptor.Slot}");
        }
    }

    public static int RequestedWorkerCount(in BepuCaseDescriptor descriptor, int threadCount)
    {
        switch (descriptor.Slot)
        {
            case BepuCaseSlot.BoxContainerPile10K:
                return BepuBoxContainerPileCase.RequestedWorkerCount(threadCount);

            default:
                throw new InvalidOperationException($"Unsupported BEPU case slot: {descriptor.Slot}");
        }
    }

    public static void StepSimulation(
        in BepuCaseDescriptor descriptor,
        Simulation simulation,
        ThreadDispatcher threadDispatcher,
        int stepCount)
    {
        switch (descriptor.Slot)
        {
            case BepuCaseSlot.BoxContainerPile10K:
                BepuBoxContainerPileCase.StepSimulation(simulation, threadDispatcher, stepCount);
                return;

            default:
                throw new InvalidOperationException($"Unsupported BEPU case slot: {descriptor.Slot}");
        }
    }

    public static void RunWarmup(in BepuCaseDescriptor descriptor, BufferPool bufferPool, int workerCount, int stepCount)
    {
        switch (descriptor.Slot)
        {
            case BepuCaseSlot.BoxContainerPile10K:
                BepuBoxContainerPileCase.RunWarmup(bufferPool, workerCount, stepCount);
                return;

            default:
                throw new InvalidOperationException($"Unsupported BEPU case slot: {descriptor.Slot}");
        }
    }

    public static BepuStabilityCounters CountStability(in BepuCaseDescriptor descriptor, Simulation simulation)
    {
        switch (descriptor.Slot)
        {
            case BepuCaseSlot.BoxContainerPile10K:
                return BepuBoxContainerPileCase.CountStability(simulation);

            default:
                throw new InvalidOperationException($"Unsupported BEPU case slot: {descriptor.Slot}");
        }
    }

    public static int SampleTransforms(in BepuCaseDescriptor descriptor, Simulation simulation, BepuTransform[] transforms)
    {
        switch (descriptor.Slot)
        {
            case BepuCaseSlot.BoxContainerPile10K:
                return BepuBoxContainerPileCase.SampleTransforms(simulation, transforms);

            default:
                throw new InvalidOperationException($"Unsupported BEPU case slot: {descriptor.Slot}");
        }
    }

    public static int CopyStaticBoxes(in BepuCaseDescriptor descriptor, BepuStaticBox[] boxes)
    {
        switch (descriptor.Slot)
        {
            case BepuCaseSlot.BoxContainerPile10K:
                return BepuBoxContainerPileCase.CopyStaticBoxes(boxes);

            default:
                throw new InvalidOperationException($"Unsupported BEPU case slot: {descriptor.Slot}");
        }
    }

    public static string CaseStatus(in BepuCaseDescriptor descriptor, BepuStabilityCounters counters)
    {
        switch (descriptor.Slot)
        {
            case BepuCaseSlot.BoxContainerPile10K:
                return BepuBoxContainerPileCase.CaseStatus(counters);

            default:
                throw new InvalidOperationException($"Unsupported BEPU case slot: {descriptor.Slot}");
        }
    }

    public static string MetricStatus(in BepuCaseDescriptor descriptor, string caseStatus)
    {
        switch (descriptor.Slot)
        {
            case BepuCaseSlot.BoxContainerPile10K:
                return BepuBoxContainerPileCase.MetricStatus(caseStatus);

            default:
                throw new InvalidOperationException($"Unsupported BEPU case slot: {descriptor.Slot}");
        }
    }
}
