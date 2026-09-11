using System;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;

internal static class SocketProbe
{
    private static nuint reporter;
    private static int failures;
    private static unsafe void Report(int phase, int value) => ((delegate* unmanaged[Cdecl]<int, int, void>)reporter)(phase, value);
    private static void Check(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
    private static void Run(int phase, string name, Func<Task> action)
    {
        Report(phase, 0);
        try { action().GetAwaiter().GetResult(); Report(phase, 1); }
        catch (Exception error) { ++failures; Report(phase, error.HResult); Console.Error.WriteLine(name + ": " + error); }
    }
    private static Socket Listener()
    {
        var socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
        socket.Bind(new IPEndPoint(IPAddress.Loopback, 0));
        socket.Listen(4);
        return socket;
    }
    private static async Task LateWrite()
    {
        using var listener = Listener();
        using var client = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
        client.SendBufferSize = 4096;
        client.Connect(listener.LocalEndPoint);
        using var server = listener.Accept();
        server.ReceiveBufferSize = 4096;
        server.ReceiveTimeout = 10000;
        using var receiveCancellation = new CancellationTokenSource();
        // Register only a read interest, then let the engine enter its poll.
        Task<int> pendingRead = client.ReceiveAsync(new byte[1], SocketFlags.None, receiveCancellation.Token).AsTask();
        await Task.Delay(250);
        client.Blocking = false;
        byte[] data = new byte[65536];
        Array.Fill(data, (byte)37);
        int sent = 0;
        bool blocked = false;
        while (sent < 8 * 1024 * 1024) {
            try { sent += client.Send(data, SocketFlags.None); }
            catch (SocketException error) when (error.SocketErrorCode == SocketError.WouldBlock) { blocked = true; break; }
        }
        Check(blocked, "Could not establish send backpressure");
        Report(81, sent);
        using var writeCancellation = new CancellationTokenSource(5000);
        Task<int> pendingWrite;
        while (true) {
            pendingWrite = client.SendAsync(data, SocketFlags.None, writeCancellation.Token).AsTask();
            if (!pendingWrite.IsCompleted) break;
            sent += await pendingWrite;
            Check(sent < 8 * 1024 * 1024, "Could not queue a send under backpressure");
        }
        Report(84, sent);
        // The peer uses blocking native reads: it must not change the managed
        // engine's registration cookie and accidentally hide a stale mask.
        Task<int> drain = Task.Run(() => {
            Thread.Sleep(250);
            int received = 0;
            byte[] buffer = new byte[65536];
            while (true) {
                int count = server.Receive(buffer);
                if (count == 0) break;
                for (int i = 0; i < count; ++i) Check(buffer[i] == 37, "Peer data corrupted");
                received += count;
            }
            return received;
        });
        try {
            int written = await pendingWrite;
            Check(written > 0, "Late queued send failed");
            client.Shutdown(SocketShutdown.Send);
            Check(await drain.WaitAsync(TimeSpan.FromSeconds(10)) == sent + written, "Peer did not receive all bytes");
        } finally {
            receiveCancellation.Cancel();
            server.Dispose();
            try { await pendingRead; } catch (OperationCanceledException) { }
            try { await drain.WaitAsync(TimeSpan.FromSeconds(10)); } catch (Exception) { }
        }
    }
    private static async Task AsyncRoundTrips()
    {
        for (int round = 0; round < 8; ++round) {
            using var listener = Listener();
            using var client = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
            using var timeout = new CancellationTokenSource(5000);
            Task<Socket> accept = listener.AcceptAsync(timeout.Token).AsTask();
            await client.ConnectAsync(listener.LocalEndPoint, timeout.Token);
            using var server = await accept;
            byte[] received = new byte[1];
            Task<int> read = server.ReceiveAsync(received, SocketFlags.None, timeout.Token).AsTask();
            Check(await client.SendAsync(new byte[] { (byte)(round + 1) }, SocketFlags.None, timeout.Token) == 1, "Async send");
            Check(await read == 1 && received[0] == round + 1, "Async receive/data");
            using var cancellation = new CancellationTokenSource(30);
            try { await client.ReceiveAsync(received, SocketFlags.None, cancellation.Token); throw new InvalidOperationException("Cancellation missing"); }
            catch (OperationCanceledException) { }
            // Close while another operation is pending, then reuse descriptors
            // in the next round. The close must settle the old operation.
            Task<int> pending = server.ReceiveAsync(received, SocketFlags.None, timeout.Token).AsTask();
            server.Dispose();
            try { await pending; throw new InvalidOperationException("Closed receive succeeded"); }
            catch (SocketException) { }
            catch (ObjectDisposedException) { }
            Report(83, round);
        }
    }
    private static async Task IdleUnreadData()
    {
        using var listener = Listener();
        using var client = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
        client.Connect(listener.LocalEndPoint);
        using var server = listener.Accept();
        byte[] data = new byte[1];
        using var cancellation = new CancellationTokenSource(50);
        try { await client.ReceiveAsync(data, SocketFlags.None, cancellation.Token); throw new InvalidOperationException("Read did not cancel"); }
        catch (OperationCanceledException) { }
        Check(server.Send(new byte[] { 71 }) == 1, "Idle payload send failed");
        await Task.Delay(100);
        Report(85, 0);
        await Task.Delay(750);
        Report(86, 750);
        Check(await client.ReceiveAsync(data, SocketFlags.None).WaitAsync(TimeSpan.FromSeconds(5)) == 1 && data[0] == 71,
            "Unread payload was not preserved");
    }
    public static int Main(string[] args)
    {
        reporter = (nuint)ulong.Parse(args[0]);
        Run(80, "Queued write after an idle read registration", LateWrite);
        Run(82, "Async accept/connect/send/receive/cancel/close", AsyncRoundTrips);
        Run(87, "Idle registered socket with unread data", IdleUnreadData);
        Report(89, failures);
        return failures == 0 ? 100 : 110;
    }
}
