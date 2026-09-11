using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

internal static unsafe class BclProbe
{
    private static delegate* unmanaged[Cdecl]<int, int, void> report;
    private static int failures;
    private static string directory;
    private static bool ownsDirectory;
    public sealed class Packet { public int Number { get; set; } public string Text { get; set; } }
    private sealed class Generic<T> { public T Echo(T value) => value; }
    public static int Compute(int value) => value * 7;
    private static void Check(bool condition) { if (!condition) throw new InvalidOperationException("BCL probe assertion"); }

    private static void Run(int phase, string name, Action action)
    {
        report(phase, 0);
        try { action(); report(phase, 1); }
        catch (Exception error) {
            ++failures;
            report(phase, error.HResult);
            Console.Error.WriteLine(name + ": " + error);
        }
    }

    private static void Files()
    {
        directory = "/switch/coreclr-bcl-owned-" + Environment.TickCount64;
        Check(!Directory.Exists(directory));
        Directory.CreateDirectory(directory);
        ownsDirectory = true;
        string path = Path.Combine(directory, "unicode.txt");
        const string text = "Celeste — Καλημέρα — 日本語\n";
        File.WriteAllText(path, text, Encoding.UTF8);
        Check(File.ReadAllText(path, Encoding.UTF8) == text);
        File.Copy(path, path + ".copy");
        File.Move(path + ".copy", path + ".moved");
        Check(Directory.GetFiles(directory).Length == 2);
        using (var handle = File.OpenHandle(path, FileMode.Open, FileAccess.Read)) {
            byte[] data = new byte[4096];
            int count = RandomAccess.Read(handle, data, 0);
            Check(count > 0 && RandomAccess.Read(handle, data, 100000) == 0);
        }
        byte[] payload = Enumerable.Range(0, 65536).Select(i => (byte)i).ToArray();
        string binary = Path.Combine(directory, "async.bin");
        File.WriteAllBytesAsync(binary, payload).WaitAsync(TimeSpan.FromSeconds(10)).GetAwaiter().GetResult();
        Check(File.ReadAllBytesAsync(binary).WaitAsync(TimeSpan.FromSeconds(10)).GetAwaiter().GetResult().SequenceEqual(payload));
        using var canceled = new CancellationTokenSource();
        canceled.Cancel();
        try { File.ReadAllBytesAsync(binary, canceled.Token).GetAwaiter().GetResult(); throw new InvalidOperationException("Expected canceled file read"); }
        catch (OperationCanceledException) { }
    }

    private static void Threads()
    {
        var completion = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        using var timer = new Timer(_ => completion.TrySetResult(123), null, 20, Timeout.Infinite);
        Check(completion.Task.WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult() == 123);
        using var gate = new SemaphoreSlim(0);
        Task worker = Task.Run(() => gate.Release());
        Check(gate.Wait(5000));
        worker.WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult();
        Task.Delay(10).WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult();
        using var canceled = new CancellationTokenSource(20);
        try { Task.Delay(Timeout.Infinite, canceled.Token).WaitAsync(TimeSpan.FromSeconds(5)).GetAwaiter().GetResult(); throw new InvalidOperationException("Expected timer cancellation"); }
        catch (OperationCanceledException) { }
    }

    private static void Reflection()
    {
        Type type = typeof(Generic<>).MakeGenericType(typeof(long));
        object instance = Activator.CreateInstance(type);
        Check((long)type.GetMethod("Echo").Invoke(instance, new object[] { 1234567890123L }) == 1234567890123L);
        var method = new DynamicMethod("GeneratedIncrement", typeof(int), new[] { typeof(int) });
        ILGenerator il = method.GetILGenerator();
        il.Emit(OpCodes.Ldarg_0); il.Emit(OpCodes.Ldc_I4_1); il.Emit(OpCodes.Add); il.Emit(OpCodes.Ret);
        Check(method.CreateDelegate<Func<int, int>>()(41) == 42);
        Check(RuntimeFeature.IsDynamicCodeSupported && RuntimeFeature.IsDynamicCodeCompiled);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference LoadAndUnload()
    {
        var context = new AssemblyLoadContext("probe-collectible", isCollectible: true);
        using var stream = File.OpenRead("/switch/coreclr-probe/Probe.dll");
        Assembly assembly = context.LoadFromStream(stream);
        Check((int)assembly.GetType("BclProbe").GetMethod("Compute").Invoke(null, new object[] { 6 }) == 42);
        var weak = new WeakReference(context);
        context.Unload();
        return weak;
    }

    private static void AssemblyLoading()
    {
        WeakReference weak = LoadAndUnload();
        for (int i = 0; i < 16 && weak.IsAlive; ++i) { GC.Collect(); GC.WaitForPendingFinalizers(); }
        Check(!weak.IsAlive);
    }

    private static void Serialization()
    {
        var value = new Packet { Number = 456, Text = "日本語 and é" };
        string json = JsonSerializer.Serialize(value);
        Packet restored = JsonSerializer.Deserialize<Packet>(json);
        Check(restored.Number == value.Number && restored.Text == value.Text);
        Check(Encoding.UTF8.GetString(Encoding.UTF8.GetBytes(value.Text)) == value.Text);
    }

    private static void Compression()
    {
        byte[] data = Enumerable.Range(0, 65536).Select(i => (byte)(i * 7)).ToArray();
        using var compressed = new MemoryStream();
        using (var gzip = new GZipStream(compressed, CompressionLevel.Optimal, leaveOpen: true)) gzip.Write(data);
        compressed.Position = 0;
        using var decoded = new MemoryStream();
        using (var gzip = new GZipStream(compressed, CompressionMode.Decompress, leaveOpen: true)) gzip.CopyTo(decoded);
        Check(decoded.ToArray().SequenceEqual(data));
        using var brotliData = new MemoryStream();
        using (var brotli = new BrotliStream(brotliData, CompressionLevel.Optimal, leaveOpen: true)) brotli.Write(data);
        brotliData.Position = 0;
        using var brotliDecoded = new MemoryStream();
        using (var brotli = new BrotliStream(brotliData, CompressionMode.Decompress, leaveOpen: true)) brotli.CopyTo(brotliDecoded);
        Check(brotliDecoded.ToArray().SequenceEqual(data));
    }

    public static int Main(string[] args)
    {
        report = (delegate* unmanaged[Cdecl]<int, int, void>)(nuint)ulong.Parse(args[0]);
        Run(60, "Console", () => Console.WriteLine("Horizon CoreCLR BCL probe"));
        Run(61, "Files and asynchronous I/O", Files);
        Run(62, "ThreadPool, timers and cancellation", Threads);
        Run(63, "Reflection, generics and Reflection.Emit", Reflection);
        Run(64, "Collectible assembly loading", AssemblyLoading);
        Run(65, "JSON and Unicode", Serialization);
        Run(66, "GZip compression", Compression);
        if (ownsDirectory) Run(69, "Owned-file cleanup", () => Directory.Delete(directory, recursive: true));
        report(70, failures);
        return failures == 0 ? 100 : 110;
    }
}
