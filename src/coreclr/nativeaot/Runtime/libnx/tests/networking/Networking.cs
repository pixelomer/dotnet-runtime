using System;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;

static class Networking
{
    [DllImport("__Internal")] static extern void ProbeReport(int phase,long value);
    [DllImport("__Internal")] static extern void ProbeError([MarshalAs(UnmanagedType.LPUTF8Str)] string error);
    static int checks;
    static void Check(bool ok) { if(!ok) throw new Exception("Network assertion failed"); checks++; }
    static Socket Create(SocketType type, ProtocolType protocol)
    {
        var socket = new Socket(AddressFamily.InterNetwork, type, protocol);
        socket.ReceiveTimeout = 2000;
        socket.SendTimeout = 2000;
        return socket;
    }
    static void SendAll(Socket socket, byte[] data)
    {
        int sent=0;
        while(sent<data.Length) {
            int n=socket.Send(data.AsSpan(sent), SocketFlags.None);
            Check(n>0); sent+=n;
        }
    }
    static void ReceiveAll(Socket socket, byte[] data)
    {
        int received=0;
        while(received<data.Length) {
            int n=socket.Receive(data.AsSpan(received), SocketFlags.None);
            Check(n>0); received+=n;
        }
    }
    static void Tcp(byte[] expected,byte[] actual)
    {
        using var listener=Create(SocketType.Stream,ProtocolType.Tcp);
        listener.Bind(new IPEndPoint(IPAddress.Loopback,0));
        listener.Listen(1);
        var endpoint=(IPEndPoint)listener.LocalEndPoint!;
        Check(endpoint.Address.Equals(IPAddress.Loopback) && endpoint.Port>0);
        using var client=Create(SocketType.Stream,ProtocolType.Tcp);
        client.NoDelay=true;
        Check(client.NoDelay);
        client.Connect(endpoint);
        using var server=listener.Accept();
        server.ReceiveTimeout=2000;server.SendTimeout=2000;
        Check(((IPEndPoint)server.RemoteEndPoint!).Address.Equals(IPAddress.Loopback));
        SendAll(client,expected);ReceiveAll(server,actual);
        Check(actual.AsSpan().SequenceEqual(expected));
        SendAll(server,actual);Array.Clear(actual);ReceiveAll(client,actual);
        Check(actual.AsSpan().SequenceEqual(expected));
        client.Shutdown(SocketShutdown.Send);
        Check(server.Receive(actual,0,1,SocketFlags.None)==0);
    }
    static void Udp(byte[] expected,byte[] actual)
    {
        using var receiver=Create(SocketType.Dgram,ProtocolType.Udp);
        using var sender=Create(SocketType.Dgram,ProtocolType.Udp);
        receiver.Bind(new IPEndPoint(IPAddress.Loopback,0));
        sender.Bind(new IPEndPoint(IPAddress.Loopback,0));
        Check(sender.SendTo(expected,0,1024,SocketFlags.None,receiver.LocalEndPoint!)==1024);
        EndPoint from=new IPEndPoint(IPAddress.Any,0);
        int n=receiver.ReceiveFrom(actual,ref from);
        Check(n==1024 && actual.AsSpan(0,n).SequenceEqual(expected.AsSpan(0,n)));
        Check(from.Equals(sender.LocalEndPoint));
        Check(receiver.SendTo(actual,0,n,SocketFlags.None,from)==n);
        Array.Clear(actual);
        n=sender.ReceiveFrom(actual,ref from);
        Check(n==1024 && actual.AsSpan(0,n).SequenceEqual(expected.AsSpan(0,n)));
        Check(from.Equals(receiver.LocalEndPoint));
    }
    [UnmanagedCallersOnly(EntryPoint="ManagedNetworkingMain")]
    public static int Main()
    {
        int phase=0;
        try {
            byte[] expected=new byte[16384],actual=new byte[16384];
            for(int round=0;round<32;round++) {
                for(int i=0;i<expected.Length;i++) expected[i]=(byte)(i*37+round);
                phase=10;Tcp(expected,actual);
                phase=20;Udp(expected,actual);
                GC.Collect();GC.WaitForPendingFinalizers();GC.Collect();
                ProbeReport(100+round,checks);
            }
            ProbeReport(1,checks);
            return 0;
        } catch(Exception ex) { ProbeReport(99,phase);ProbeError(ex.ToString());return 1; }
    }
}
