using System;
using System.Runtime.CompilerServices;
using System.Threading;

internal static unsafe class Stress
{
    [ThreadStatic] private static int token;
    private static int ready, start, failed, finalized;
    private sealed class Root { public int Value; public Root(int value) => Value = value; }
    private sealed class Finalizable { ~Finalizable() => Interlocked.Increment(ref finalized); }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int Read(Root root) => root.Value;
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void MakeFinalizable() { for (int i = 0; i < 32; ++i) _ = new Finalizable(); }
    private static void Check(bool value) { if (!value) throw new InvalidOperationException("CoreCLR stress assertion"); }
    private static void Worker(object state)
    {
        try {
            int id = (int)state;
            Check(token == 0); token = id;
            Root root = new Root(id * 7);
            Interlocked.Increment(ref ready);
            while (Volatile.Read(ref start) == 0) Thread.Yield();
            for (int round = 0; round < 128; ++round) {
                byte[] data = new byte[32768]; data[round] = (byte)round;
                Check(token == id && Read(root) == id * 7 && data[round] == (byte)round);
                if ((round & 15) == 0) GC.Collect();
                Thread.Yield();
                GC.KeepAlive(data);
            }
            Check(Read(root) == id * 7 && token == id);
        } catch { Interlocked.Increment(ref failed); }
    }
    public static int Main(string[] args)
    {
        // The native host explicitly supplies its reporting function pointer.
        // This exercises the unmanaged call ABI without inventing library lookup.
        var report = (delegate* unmanaged[Cdecl]<int, int, void>)(nuint)ulong.Parse(args[0]);
        report(1, 0);
        int caught = 0;
        for (int i = 0; i < 32; ++i) {
            try { _ = Read(null); } catch (NullReferenceException) { ++caught; }
            try { throw new InvalidOperationException("explicit"); } catch (InvalidOperationException) { ++caught; }
        }
        Check(caught == 64); report(2, caught);
        token = 12345;
        Thread[] workers = new Thread[4];
        for (int i = 0; i < workers.Length; ++i) { workers[i] = new Thread(Worker); workers[i].Start(i + 1); }
        while (Volatile.Read(ref ready) != 4 && Volatile.Read(ref failed) == 0) Thread.Yield();
        Check(failed == 0); report(3, ready);
        Volatile.Write(ref start, 1);
        for (int i = 0; i < 16; ++i) GC.Collect();
        foreach (Thread worker in workers) Check(worker.Join(10000));
        Check(failed == 0 && token == 12345); report(4, failed);
        MakeFinalizable(); GC.Collect(); GC.WaitForPendingFinalizers();
        Check(finalized == 32); report(5, finalized);
        return 100;
    }
}
