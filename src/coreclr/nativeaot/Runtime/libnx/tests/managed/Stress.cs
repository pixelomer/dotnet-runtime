using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;

static class Stress
{
    [DllImport("__Internal", EntryPoint="ProbeReport")]
    static extern void Report(int phase, long value);
    [DllImport("__Internal", EntryPoint="ProbeCommittedBytes")]
    static extern ulong Committed();
    [ThreadStatic] static int threadToken;
    static int finalized;
    sealed class Token { public readonly int Value; public Token(int value) => Value=value; }
    sealed class Busy
    {
        public readonly Token Token;
        public bool Stop;
        public bool Ready;
        public int Observed;
        public Busy(int value) => Token=new Token(value);
    }
    sealed class Finalizable { ~Finalizable() => Interlocked.Increment(ref finalized); }
    static void Check(bool condition) { if (!condition) throw new Exception("Managed stress assertion failed"); }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static int ReadToken(Token token) => token.Value;
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void Exceptions()
    {
        int caught=0, cleaned=0;
        for (int i=0;i<100;i++)
        {
            try { try { _=ReadToken(null!); } finally { cleaned++; } }
            catch (NullReferenceException) { caught++; }
            try { throw new InvalidOperationException("explicit"); }
            catch (InvalidOperationException) { caught++; }
        }
        Check(caught==200 && cleaned==100);
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void BusyLoop(object? value)
    {
        Busy state=(Busy)value!;
        Token token=state.Token;
        threadToken=token.Value;
        Volatile.Write(ref state.Ready,true);
        while (!Volatile.Read(ref state.Stop)) state.Observed=token.Value;
        Check(state.Observed==token.Value && threadToken==token.Value);
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static WeakReference AllocateBatch()
    {
        byte[][] arrays=new byte[512][];
        for(int i=0;i<arrays.Length;i++)
        {
            arrays[i]=new byte[16384];
            arrays[i][0]=(byte)i;
            arrays[i][^1]=(byte)(i^0x5a);
        }
        for(int i=0;i<arrays.Length;i++)
            Check(arrays[i][0]==(byte)i && arrays[i][^1]==(byte)(i^0x5a));
        return new WeakReference(arrays);
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void CreateFinalizers() { for(int i=0;i<100;i++) _=new Finalizable(); }
    [UnmanagedCallersOnly(EntryPoint="ManagedStressMain")]
    public static int Run()
    {
        try
        {
            finalized=0;
            Report(1,GC.GetTotalMemory(false));
            threadToken=0x1234;
            Exceptions();
            Report(2,200);
            Busy[] busy=new Busy[4];
            Thread[] threads=new Thread[4];
            for(int i=0;i<threads.Length;i++)
            {
                busy[i]=new Busy(0x100+i);
                threads[i]=new Thread(BusyLoop);
                threads[i].Start(busy[i]);
            }
            foreach(Busy item in busy)
                while(!Volatile.Read(ref item.Ready)) Thread.Sleep(1);
            for(int i=0;i<64;i++)
            {
                WeakReference weak=AllocateBatch();
                GC.Collect(2,GCCollectionMode.Forced,true,true);
                Check(!weak.IsAlive);
                if(i%8==0) Report(3,(long)Committed());
            }
            foreach(Busy item in busy) Volatile.Write(ref item.Stop,true);
            foreach(Thread thread in threads) Check(thread.Join(5000));
            Check(threadToken==0x1234);
            Report(4,64);
            // Concurrent hardware faults exercise per-thread exception storage.
            for(int i=0;i<threads.Length;i++) threads[i]=new Thread(Exceptions);
            foreach(Thread thread in threads) thread.Start();
            foreach(Thread thread in threads) Check(thread.Join(5000));
            Report(5,800);
            CreateFinalizers();
            GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
            Check(Volatile.Read(ref finalized)==100);
            Report(6,finalized);
            Report(7,(long)Committed());
            return 0;
        }
        catch(Exception ex)
        {
            Report(-1,ex.HResult);
            return 1;
        }
    }
}
