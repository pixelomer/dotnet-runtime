using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Threading;

internal static unsafe class Soak
{
    private sealed class Root { public long Value; public byte[] Payload = new byte[4096]; }
    private sealed class Finalizable { ~Finalizable() => Interlocked.Increment(ref finalized); }
    [ThreadStatic] private static int token;
    private static int ready, failed, moved, finalized;
    private static int* control;
    private static delegate* unmanaged[Cdecl]<int, int, void> report;
    public static int Compute(int value) => value * 7;
    private static void Check(bool value, string message) { if (!value) throw new InvalidOperationException(message); }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static nuint Address(Root root) => (nuint)Unsafe.AsPointer(ref root.Value);
    // Intentionally eligible for Tier0 and on-stack replacement. No explicit
    // managed/native calls or voluntary transitions inside this hot loop.
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void HotLoop(Root root, ref byte interior)
    {
        while (Volatile.Read(ref control[0]) == 0) {
            root.Value++;
            if (interior != 67) Volatile.Write(ref failed, 1);
        }
    }
    private static void BusyWorker(object state)
    {
        try {
            int id = (int)state;
            Check(token == 0, "Dirty thread-local state"); token = id;
            byte[] gap = new byte[32768]; gap[1] = 1; GC.KeepAlive(gap);
            Root root = new Root(); root.Payload[123] = 67;
            ref byte interior = ref root.Payload[123];
            nuint before = Address(root);
            Interlocked.Increment(ref ready);
            HotLoop(root, ref interior);
            Check(root.Value > 0 && interior == 67 && root.Payload[123] == 67 && token == id, "Busy root/TLS corruption");
            if (Address(root) != before) Interlocked.Increment(ref moved);
            GC.KeepAlive(root);
        } catch (Exception error) { Interlocked.Increment(ref failed); Console.Error.WriteLine(error); }
    }
    private static void ChurnWorker(object state)
    {
        try {
            int id = (int)state;
            Check(token == 0, "Reused thread inherited TLS"); token = id;
            for (int i = 0; i < 128; ++i) {
                byte[] data = new byte[4096]; data[i] = (byte)i;
                Check(data[i] == (byte)i && token == id, "Churn allocation/TLS");
                GC.KeepAlive(data);
            }
        } catch (Exception error) { Interlocked.Increment(ref failed); Console.Error.WriteLine(error); }
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference Allocate()
    {
        byte[][] arrays = new byte[64][];
        for (int i = 0; i < arrays.Length; ++i) {
            arrays[i] = new byte[262144]; arrays[i][0] = (byte)i; arrays[i][^1] = (byte)(i ^ 93);
        }
        for (int i = 0; i < arrays.Length; ++i) Check(arrays[i][0] == i && arrays[i][^1] == (byte)(i ^ 93), "Allocation data");
        return new WeakReference(arrays);
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void MakeFinalizers() { for (int i = 0; i < 32; ++i) _ = new Finalizable(); }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference LoadAndUnload()
    {
        var context = new AssemblyLoadContext("soak", isCollectible: true);
        using var stream = File.OpenRead("/switch/coreclr-probe/Probe.dll");
        Assembly assembly = context.LoadFromStream(stream);
        Check((int)assembly.GetType("Soak").GetMethod("Compute").Invoke(null, new object[] { 6 }) == 42, "Collectible invocation");
        var weak = new WeakReference(context); context.Unload(); return weak;
    }
    public static int Main(string[] args)
    {
        report = (delegate* unmanaged[Cdecl]<int, int, void>)(nuint)ulong.Parse(args[0]);
        control = (int*)(nuint)ulong.Parse(args[1]);
        Thread[] busy = new Thread[4];
        int rounds = 0;
        try {
            for (int i = 0; i < busy.Length; ++i) { busy[i] = new Thread(BusyWorker); busy[i].Start(i + 1); }
            long readyDeadline = Environment.TickCount64 + 10000;
            while (Volatile.Read(ref ready) != 4 && Volatile.Read(ref failed) == 0 && Environment.TickCount64 < readyDeadline) Thread.Sleep(1);
            Check(ready == 4 && failed == 0, "Busy workers failed to start");
            long begin = Environment.TickCount64;
            do {
                // The three-second native deadline covers the whole round,
                // including collections triggered by allocation or unloading.
                report(50, rounds);
                WeakReference arrays = Allocate();
                Thread[] threads = new Thread[4];
                for (int i = 0; i < threads.Length; ++i) { threads[i] = new Thread(ChurnWorker); threads[i].Start(rounds * 4 + i + 100); }
                foreach (Thread thread in threads) Check(thread.Join(10000), "Thread retirement timeout");
                MakeFinalizers();
                WeakReference context = LoadAndUnload();
                for (int i = 0; i < 16 && (i < 2 || context.IsAlive || arrays.IsAlive); ++i) {
                    GC.Collect(2, GCCollectionMode.Forced, blocking: true, compacting: true);
                    GC.WaitForPendingFinalizers();
                }
                Check(!context.IsAlive && !arrays.IsAlive && finalized == (rounds + 1) * 32 && failed == 0, "Lifetime/finalizer check");
                report(51, rounds);
                report(90, rounds);
                report(91, checked((int)GC.GetTotalMemory(false)));
                ++rounds;
                if (Volatile.Read(ref control[1]) != 0) break;
                Thread.Sleep(1000);
            } while (Environment.TickCount64 - begin < 180000 && rounds < 180);
        } finally {
            report(53, 0);
            foreach (Thread thread in busy) if (thread != null) Check(thread.Join(10000), "Busy worker did not stop");
        }
        report(52, Volatile.Read(ref control[1])); report(54, moved); report(92, rounds);
        Check(failed == 0 && moved > 0, "Busy worker relocation/state");
        return Volatile.Read(ref control[1]) == 0 ? 100 : 101;
    }
}
