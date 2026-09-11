using System;
using System.Reflection;
using System.Runtime.Loader;
internal static unsafe class ReadyToRunProbe
{
    public static int Main(string[] args)
    {
        var report = (delegate* unmanaged[Cdecl]<int, int, void>)(nuint)ulong.Parse(args[0]);
        report(100, 0);
        try {
            Assembly assembly = AssemblyLoadContext.Default.LoadFromAssemblyPath("/switch/coreclr-probe/OwnedReadyToRun.dll");
            MethodInfo method = assembly.GetType("OwnedReadyToRun").GetMethod("Compute");
            if ((int)method.Invoke(null, new object[] { 6 }) != 42) throw new InvalidOperationException("ReadyToRun IL result");
            try { method.Invoke(null, new object[] { int.MaxValue }); throw new InvalidOperationException("Missing overflow"); }
            catch (TargetInvocationException error) when (error.InnerException is OverflowException) { }
            if (!ReferenceEquals(Assembly.Load("OwnedReadyToRun"), assembly)) throw new InvalidOperationException("Default-context identity");
            report(100, 1);
            return 100;
        } catch (Exception error) { report(100, error.HResult); Console.Error.WriteLine(error); return 110; }
    }
}
