using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

static class Callbacks
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate long IntegerCallback(long a,long b,long c,long d,long e,long f,long g,long h,long i,long j);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate double FloatCallback(double a,double b,double c,double d,double e,double f,double g,double h,double i,double j);
    [DllImport("__Internal")] static extern long InvokeInteger(IntPtr callback, int worker);
    [DllImport("__Internal")] static extern double InvokeFloat(IntPtr callback);
    [DllImport("__Internal")] static extern int CheckThunkPermissions(IntPtr callback);
    [DllImport("__Internal")] static extern void ProbeReport(int phase, long value);
    static void Check(bool condition) { if (!condition) throw new Exception("Callback assertion failed"); }
    static IntegerCallback Make(int token) => (a,b,c,d,e,f,g,h,i,j) => a+2*b+3*c+4*d+5*e+6*f+7*g+8*h+9*i+10*j+token;
    static void Collect() { GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect(); }

    [MethodImpl(MethodImplOptions.NoInlining)]
    static void Batch(int round)
    {
        var callbacks = new IntegerCallback[5000];
        var pointers = new IntPtr[callbacks.Length];
        for (int i=0;i<callbacks.Length;i++) {
            callbacks[i]=Make(i+round);
            pointers[i]=Marshal.GetFunctionPointerForDelegate(callbacks[i]);
        }
        Collect();
        Check(CheckThunkPermissions(pointers[0])==1);
        for(int i=0;i<callbacks.Length;i++) {
            Check(InvokeInteger(pointers[i],0)==385+i+round);
            Check(ReferenceEquals(callbacks[i],Marshal.GetDelegateForFunctionPointer<IntegerCallback>(pointers[i])));
        }
        for(int i=0;i<16;i++) Check(InvokeInteger(pointers[i*255],1)==385+i*255+round);
        GC.KeepAlive(callbacks);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    static int Exhaust()
    {
        var keep=new IntegerCallback[20000];
        int count=0;
        try {
            for (;count<keep.Length;count++) {
                keep[count]=Make(count);
                _=Marshal.GetFunctionPointerForDelegate(keep[count]);
            }
        } catch (OutOfMemoryException) {
            Check(count>=16000 && count<=16320);
            Check(InvokeInteger(Marshal.GetFunctionPointerForDelegate(keep[0]),0)==385);
            GC.KeepAlive(keep);
            return count;
        }
        throw new Exception("Expected finite callback pool exhaustion");
    }

    [UnmanagedCallersOnly(EntryPoint="ManagedCallbacksMain")]
    public static int Main()
    {
        try {
            double token=0.25;
            FloatCallback fp=(a,b,c,d,e,f,g,h,i,j)=>a+2*b+3*c+4*d+5*e+6*f+7*g+8*h+9*i+10*j+token;
            Check(InvokeFloat(Marshal.GetFunctionPointerForDelegate(fp))==385.25);
            for(int round=0;round<8;round++) {
                Batch(round);
                Collect();
                ProbeReport(1,round);
            }
            GC.KeepAlive(fp);
            ProbeReport(2,Exhaust());
            Collect();
            Batch(9);
            ProbeReport(3,1);
            return 0;
        } catch (Exception) { ProbeReport(99,1); return 1; }
    }
}
