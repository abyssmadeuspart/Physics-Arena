namespace Bas3D.BenchmarkPolygon.BepuPhysics2;

public static class BepuPolygonRunner
{
    public static int Run(string[] args)
    {
        int parseStatus = BepuRunnerArgsParser.Parse(args, out BepuRunnerArgs runnerArgs);
        if (parseStatus != 0)
        {
            return parseStatus;
        }

        return BepuCaseRegistry.RunHeadless(runnerArgs);
    }

}
