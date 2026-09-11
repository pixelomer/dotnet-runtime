using System.Runtime.CompilerServices;
public static class OwnedReadyToRun
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static int Compute(int value) => checked(value * 7);
}
