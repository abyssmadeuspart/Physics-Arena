namespace Bas3D.BenchmarkPolygon.UnityPhysics
{
    public enum RecordingMode : byte
    {
        Off,
        On
    }

    public enum VerificationMode : byte
    {
        On,
        Off
    }

    public struct RunnerArgs
    {
        public CaseExecutionSpec CaseExecution;
        public CaseExecutionToggle SolverStabilization;
        public string OutputPath;
        public string StackStream;
        public string RecordingPath;
        public RecordingMode RecordingMode;
        public VerificationMode VerificationMode;
        public string StepTimingOutputPath;
        public UnityPhysicsCaseRegistration CaseRegistration;
        public int ThreadCount;
        public int StepCount;
        public int WarmupSteps;
        public int RepeatIndex;
    }
}
