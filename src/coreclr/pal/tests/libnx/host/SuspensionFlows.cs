using System;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using System.Threading;

internal static unsafe class SuspensionFlows
{
    private sealed class Root { public long Value; public byte[] Payload = new byte[4096]; }
    private delegate void Loop(nint control, Root root, ref byte interior, int choice);
    private static int* control;
    private static int ready, failed, moved;
    private static Loop[] loops;
    private static readonly Type[] parameters = { typeof(nint), typeof(Root), typeof(byte).MakeByRefType(), typeof(int) };
    private static readonly FieldInfo valueField = typeof(Root).GetField("Value");
    private static readonly FieldInfo failureField = typeof(SuspensionFlows).GetField("failed", BindingFlags.NonPublic | BindingFlags.Static);
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static nuint Address(Root root) => (nuint)Unsafe.AsPointer(ref root.Value);
    private static DynamicMethod Method(string name) => new DynamicMethod(name, typeof(void), parameters, typeof(SuspensionFlows), true);
    private static void Body(ILGenerator il, Label exit)
    {
        il.Emit(OpCodes.Ldarg_0); il.Emit(OpCodes.Volatile); il.Emit(OpCodes.Ldind_I4); il.Emit(OpCodes.Brtrue, exit);
        il.Emit(OpCodes.Ldarg_1); il.Emit(OpCodes.Dup); il.Emit(OpCodes.Ldfld, valueField);
        il.Emit(OpCodes.Ldc_I4_1); il.Emit(OpCodes.Conv_I8); il.Emit(OpCodes.Add); il.Emit(OpCodes.Stfld, valueField);
        Label valid = il.DefineLabel();
        il.Emit(OpCodes.Ldarg_2); il.Emit(OpCodes.Ldind_U1); il.Emit(OpCodes.Ldc_I4, 67); il.Emit(OpCodes.Beq, valid);
        il.Emit(OpCodes.Ldc_I4_1); il.Emit(OpCodes.Stsfld, failureField);
        il.MarkLabel(valid);
    }
    private static void TailBody(DynamicMethod method, DynamicMethod peer)
    {
        ILGenerator il = method.GetILGenerator();
        Label exit = il.DefineLabel();
        Body(il, exit);
        il.Emit(OpCodes.Ldarg_0); il.Emit(OpCodes.Ldarg_1); il.Emit(OpCodes.Ldarg_2); il.Emit(OpCodes.Ldarg_3);
        il.Emit(OpCodes.Tailcall); il.Emit(OpCodes.Call, peer); il.Emit(OpCodes.Ret);
        il.MarkLabel(exit); il.Emit(OpCodes.Ret);
    }
    private static Loop Irreducible()
    {
        DynamicMethod method = Method("GeneratedIrreducibleLoop");
        ILGenerator il = method.GetILGenerator();
        Label a = il.DefineLabel(), b = il.DefineLabel(), exit = il.DefineLabel();
        // A and B each have an entry from outside their strongly connected
        // component, and both self/cross edges depend on changing object data.
        il.Emit(OpCodes.Ldarg_3); il.Emit(OpCodes.Brtrue, b); il.Emit(OpCodes.Br, a);
        il.MarkLabel(a); Body(il, exit);
        il.Emit(OpCodes.Ldarg_1); il.Emit(OpCodes.Ldfld, valueField); il.Emit(OpCodes.Ldc_I4_7); il.Emit(OpCodes.Conv_I8);
        il.Emit(OpCodes.And); il.Emit(OpCodes.Brtrue, a); il.Emit(OpCodes.Br, b);
        il.MarkLabel(b); Body(il, exit);
        il.Emit(OpCodes.Ldarg_1); il.Emit(OpCodes.Ldfld, valueField); il.Emit(OpCodes.Ldc_I4_3); il.Emit(OpCodes.Conv_I8);
        il.Emit(OpCodes.And); il.Emit(OpCodes.Brtrue, b); il.Emit(OpCodes.Br, a);
        il.MarkLabel(exit); il.Emit(OpCodes.Ret);
        return method.CreateDelegate<Loop>();
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void Worker(object state)
    {
        int mode = (int)state;
        byte[] gap = new byte[32768]; gap[12] = 34; GC.KeepAlive(gap);
        Root root = new Root(); root.Payload[123] = 67;
        ref byte interior = ref root.Payload[123];
        nuint before = Address(root);
        Interlocked.Increment(ref ready);
        loops[mode]((nint)control, root, ref interior, mode & 1);
        if (root.Value == 0 || interior != 67 || root.Payload[123] != 67) Volatile.Write(ref failed, 1);
        if (Address(root) != before) Interlocked.Increment(ref moved);
        GC.KeepAlive(root);
    }
    public static int Main(string[] args)
    {
        var report = (delegate* unmanaged[Cdecl]<int, int, void>)(nuint)ulong.Parse(args[0]);
        control = (int*)(nuint)ulong.Parse(args[1]);
        DynamicMethod a = Method("GeneratedTailCycleA"), b = Method("GeneratedTailCycleB");
        TailBody(a, b); TailBody(b, a);
        Loop irreducible = Irreducible();
        loops = new[] { a.CreateDelegate<Loop>(), b.CreateDelegate<Loop>(), irreducible, irreducible };
        foreach (Loop loop in loops) RuntimeHelpers.PrepareDelegate(loop);
        Thread[] workers = new Thread[4];
        for (int i = 0; i < 4; ++i) { workers[i] = new Thread(Worker); workers[i].Start(i); }
        while (Volatile.Read(ref ready) != 4) Thread.Yield();
        Thread.Sleep(100);
        for (int i = 0; i < 32; ++i) {
            report(50, i);
            GC.Collect(2, GCCollectionMode.Forced, blocking: true, compacting: true);
            report(51, i);
            if (Volatile.Read(ref control[1]) != 0) break;
        }
        report(53, 0);
        foreach (Thread worker in workers) if (!worker.Join(10000)) throw new InvalidOperationException("Generated loop did not stop");
        int timeout = Volatile.Read(ref control[1]);
        report(52, timeout); report(54, moved);
        if (failed != 0 || (timeout == 0 && moved == 0)) throw new InvalidOperationException("Generated loop root/relocation check failed");
        return timeout == 0 ? 100 : 101;
    }
}
