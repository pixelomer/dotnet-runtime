using System;
using System.Runtime.CompilerServices;
using System.Threading;

internal static unsafe class Suspension
{
    private sealed class Root { public long Value; public byte[] Payload = new byte[4096]; }
    private static int ready, failed, moved;
    private static int* control;

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static nuint Address(Root root) => (nuint)Unsafe.AsPointer(ref root.Value);

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void EnterFinally(Root root) { if (root.Value < 0) throw new InvalidOperationException(); }

    [MethodImpl(MethodImplOptions.NoInlining | MethodImplOptions.AggressiveOptimization)]
    private static void Worker(object state)
    {
        // Leave a reclaimable gap before the roots so compaction can move them.
        byte[] garbage = new byte[32768];
        garbage[123] = 19;
        GC.KeepAlive(garbage);
        Root root = new Root();
        root.Payload[123] = 67;
        ref byte interior = ref root.Payload[123];
        // Separate calls prevent value numbering from folding two expressions
        // for the same field address into a constant comparison across the GC.
        nuint before = Address(root);
        int mode = (int)state;
        // No managed/native calls or voluntary GC transitions in this loop.
        // The native watchdog releases it after three seconds on a stalled GC.
        switch (mode) {
            case 0:
                Interlocked.Increment(ref ready);
                while (Volatile.Read(ref control[0]) == 0) {
                    root.Value++;
                    if (interior != 67) Volatile.Write(ref failed, 1);
                }
                break;
            case 1:
                try {
                    Interlocked.Increment(ref ready);
                    while (Volatile.Read(ref control[0]) == 0) {
                        root.Value++;
                        if (interior != 67) Volatile.Write(ref failed, 1);
                    }
                } finally { if (interior != 67) Volatile.Write(ref failed, 1); }
                break;
            case 2:
                try { throw new InvalidOperationException("catch-loop probe"); }
                catch (InvalidOperationException) {
                    Interlocked.Increment(ref ready);
                    while (Volatile.Read(ref control[0]) == 0) {
                        root.Value++;
                        if (interior != 67) Volatile.Write(ref failed, 1);
                    }
                }
                break;
            default:
                try { EnterFinally(root); }
                finally {
                    Interlocked.Increment(ref ready);
                    while (Volatile.Read(ref control[0]) == 0) {
                        root.Value++;
                        if (interior != 67) Volatile.Write(ref failed, 1);
                    }
                }
                break;
        }
        if (root.Value == 0 || root.Payload[123] != 67) Volatile.Write(ref failed, 1);
        if (Address(root) != before) Interlocked.Increment(ref moved);
        GC.KeepAlive(root);
    }

    public static int Main(string[] args)
    {
        var report = (delegate* unmanaged[Cdecl]<int, int, void>)(nuint)ulong.Parse(args[0]);
        control = (int*)(nuint)ulong.Parse(args[1]);
        Thread[] workers = new Thread[4];
        for (int i = 0; i < workers.Length; ++i) { workers[i] = new Thread(Worker); workers[i].Start(i); }
        while (Volatile.Read(ref ready) != workers.Length) Thread.Yield();
        for (int i = 0; i < 32; ++i) {
            report(50, i);
            GC.Collect(2, GCCollectionMode.Forced, blocking: true, compacting: true);
            report(51, i);
            if (Volatile.Read(ref control[1]) != 0) break;
        }
        report(53, 0);
        foreach (Thread worker in workers)
            if (!worker.Join(10000)) throw new InvalidOperationException("Suspension worker failed to finish");
        int timeout = Volatile.Read(ref control[1]);
        report(52, timeout);
        report(54, moved);
        if (failed != 0) throw new InvalidOperationException("Managed root corrupted during suspension");
        if (timeout == 0 && moved == 0) throw new InvalidOperationException("No worker root relocated; compaction was not exercised");
        return timeout == 0 ? 100 : 101;
    }
}
