namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
    public struct RunnerArgs
    {
        public string CaseId;
        public string OutputPath;
        public UnityPhysicsCaseDescriptor CaseDescriptor;
        public int ThreadCount;
        public int StepCount;
        public int WarmupSteps;
        public int RepeatIndex;
    }
}
