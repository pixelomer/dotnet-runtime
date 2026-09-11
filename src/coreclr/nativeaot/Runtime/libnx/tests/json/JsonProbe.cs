using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;

class Model { public string Name { get; set; }=""; public int Number { get; set; } }
[JsonSourceGenerationOptions(WriteIndented=true,AllowTrailingCommas=true)]
[JsonSerializable(typeof(List<Model>))]
partial class ModelContext : JsonSerializerContext { }
static class JsonProbe
{
    [DllImport("__Internal")] static extern void ProbeReport(int phase,long value);
    [DllImport("__Internal")] static extern void ProbeError([MarshalAs(UnmanagedType.LPUTF8Str)] string value);
    [UnmanagedCallersOnly(EntryPoint="ManagedJsonProbeMain")]
    public static int Main()
    {
        try {
            ProbeReport(1,1);
            var hash=new HashCode(); hash.Add(false);hash.Add("text");hash.Add(new object());
            ProbeReport(2,hash.ToHashCode());
            ProbeError(typeof(List<Model>).ToString());
            var context=ModelContext.Default;
            ProbeReport(3,1);
            var info=context.ListModel;
            ProbeReport(4,1);
            var value=JsonSerializer.Deserialize("[{\"Name\":\"a\",\"Number\":42}]",info)!;
            if(value.Count!=1 || value[0].Number!=42) throw new Exception("JSON deserialize mismatch");
            ProbeReport(5,1);
            var encoded=JsonSerializer.Serialize(value,info);
            if(!encoded.Contains("42")) throw new Exception("JSON serialize mismatch");
            ProbeReport(6,1);
            Task[] tasks=new Task[3];
            for(int worker=0;worker<tasks.Length;worker++) tasks[worker]=Task.Run(()=> {
                for(int i=0;i<500;i++) {
                    var local=JsonSerializer.Deserialize("[{\"Name\":\"worker\",\"Number\":42}]",ModelContext.Default.ListModel)!;
                    if(local[0].Number!=42) throw new Exception("Concurrent JSON mismatch");
                    byte[] bytes=new byte[16384]; bytes[0]=42;
                    if(i%20==0) GC.Collect(2,GCCollectionMode.Forced,true,true);
                    GC.KeepAlive(bytes);
                }
            });
            for(int i=0;i<100;i++) {
                _=typeof(List<Model>).ToString();
                GC.Collect(2,GCCollectionMode.Forced,true,true);
                Thread.Yield();
            }
            Task.WaitAll(tasks);
            ProbeReport(7,1);
            return 0;
        } catch(Exception ex) { ProbeError(ex.ToString());return 1; }
    }
}
