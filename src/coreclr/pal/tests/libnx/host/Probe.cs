using System;
using System.Runtime.CompilerServices;
internal static class Probe
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int Sum(int[] values) { int sum = 0; foreach (int value in values) sum += value; return sum; }
    public static int Main()
    {
        int[] values = new int[64];
        for (int i = 0; i < values.Length; ++i) values[i] = i;
        if (Sum(values) != 2016) return 1;
        GC.Collect();
        if (Sum(values) != 2016) return 2;
        try { throw new InvalidOperationException("CoreCLR Horizon probe"); }
        catch (InvalidOperationException) { return 100; }
    }
}
