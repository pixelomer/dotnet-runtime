using System;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;

static class Networking
{
    [DllImport("__Internal")] static extern void ProbeReport(int phase,long value);
    [DllImport("__Internal")] static extern void ProbeError([MarshalAs(UnmanagedType.LPUTF8Str)] string error);
    static int checks, operation;
    static void Check(bool ok) { if(!ok) throw new Exception("Network assertion failed"); checks++; }
    static Socket Create(SocketType type, ProtocolType protocol)
    {
        operation=1;
        var socket = new Socket(AddressFamily.InterNetwork, type, protocol);
        operation=2;socket.ReceiveTimeout = 2000;
        operation=3;socket.SendTimeout = 2000;
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
        operation=11;listener.Bind(new IPEndPoint(IPAddress.Loopback,0));
        operation=12;listener.Listen(1);
        operation=13;var endpoint=(IPEndPoint)listener.LocalEndPoint!;
        Check(endpoint.Address.Equals(IPAddress.Loopback) && endpoint.Port>0);
        using var client=Create(SocketType.Stream,ProtocolType.Tcp);
        operation=14;client.NoDelay=true;
        Check(client.NoDelay);
        operation=15;client.Connect(endpoint);
        operation=16;using var server=listener.Accept();
        operation=17;server.ReceiveTimeout=2000;server.SendTimeout=2000;
        Check(((IPEndPoint)server.RemoteEndPoint!).Address.Equals(IPAddress.Loopback));
        operation=18;SendAll(client,expected);ReceiveAll(server,actual);
        Check(actual.AsSpan().SequenceEqual(expected));
        operation=19;SendAll(server,actual);Array.Clear(actual);ReceiveAll(client,actual);
        Check(actual.AsSpan().SequenceEqual(expected));
        operation=20;client.Shutdown(SocketShutdown.Send);
        Check(server.Receive(actual,0,1,SocketFlags.None)==0);
    }
    static void Udp(byte[] expected,byte[] actual)
    {
        using var receiver=Create(SocketType.Dgram,ProtocolType.Udp);
        using var sender=Create(SocketType.Dgram,ProtocolType.Udp);
        operation=21;receiver.Bind(new IPEndPoint(IPAddress.Loopback,0));
        operation=22;sender.Bind(new IPEndPoint(IPAddress.Loopback,0));
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
                ProbeReport(200+round,checks);
                phase=10;Tcp(expected,actual);
                ProbeReport(300+round,checks);
                phase=20;Udp(expected,actual);
                ProbeReport(400+round,checks);
                GC.Collect();GC.WaitForPendingFinalizers();GC.Collect();
                ProbeReport(100+round,checks);
            }
            ProbeReport(1,checks);
            return 0;
        } catch(Exception ex) {
            ProbeReport(99,phase);ProbeReport(98,operation);ProbeReport(97,ex.HResult);
            ProbeError(ex.GetType().FullName ?? "unknown exception");
            if(ex is SocketException socket) ProbeReport(96,(int)socket.SocketErrorCode);
            return 1;
        }
    }
}
