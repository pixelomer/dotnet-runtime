using System.Runtime.InteropServices;
internal static class Entry
{
    [UnmanagedCallersOnly(EntryPoint = "ManagedSocketMain")]
    public static int Run(nuint reporter) => SocketProbe.Main(new[] { reporter.ToString() });
}
