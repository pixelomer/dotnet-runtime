using System;
using System.IO;
using System.Runtime.InteropServices;

static class FileSystem
{
    [DllImport("__Internal")] static extern void ProbeReport(int phase,long value);
    [DllImport("__Internal")] static extern void ProbeError([MarshalAs(UnmanagedType.LPUTF8Str)] string error);
    static void Check(bool ok) { if(!ok) throw new Exception("Filesystem assertion failed"); }
    [UnmanagedCallersOnly(EntryPoint="ManagedFileSystemMain")]
    public static int Main()
    {
        const string root="sdmc:/switch/nativeaot-filesystem-test";
        int phase=0;
        try {
            Check(Path.IsPathRooted(root));
            Check(Path.GetPathRoot(root)=="sdmc:/");
            Check(Path.GetPathRoot("romfs:/Content")=="romfs:/");
            Check(Path.GetFullPath(root+"/a/../b")==root+"/b");
            Check(Path.GetFullPath("romfs:/Content/./a/../b")=="romfs:/Content/b");
            ProbeReport(++phase,1);
            Check(!Directory.Exists(root));
            Directory.CreateDirectory(root);
            string previous=Environment.CurrentDirectory;
            Environment.CurrentDirectory=root;
            Check(Path.GetFullPath("relative.bin")==root+"/relative.bin");
            byte[] expected=new byte[8192];
            for(int i=0;i<expected.Length;i++) expected[i]=(byte)(i*37);
            File.WriteAllBytes("relative.bin",expected);
            Check(File.ReadAllBytes("relative.bin").AsSpan().SequenceEqual(expected));
            ProbeReport(++phase,1);
            using(var stream=new FileStream("relative.bin",FileMode.Open,FileAccess.ReadWrite,FileShare.ReadWrite)) {
                stream.Position=123;
                byte[] chunk=new byte[257];
                RandomAccess.Read(stream.SafeFileHandle,chunk,510);
                Check(chunk.AsSpan().SequenceEqual(expected.AsSpan(510,257)));
                Check(stream.Position==123);
                Array.Fill(chunk,(byte)0x6b);
                RandomAccess.Write(stream.SafeFileHandle,chunk,510);
                chunk.CopyTo(expected,510);
                Check(stream.Position==123);
                stream.Seek(0,SeekOrigin.Begin);
                byte[] actual=new byte[expected.Length];
                stream.ReadExactly(actual);
                Check(actual.AsSpan().SequenceEqual(expected));
                stream.Flush(true);
            }
            ProbeReport(++phase,1);
            File.Copy("relative.bin","copy.bin");
            File.Move("copy.bin","moved.bin");
            Check(Directory.GetFiles(root,"*.bin").Length==2);
            Check(new FileInfo("moved.bin").Length==expected.Length);
            Check(File.ReadAllBytes("moved.bin").AsSpan().SequenceEqual(expected));
            ProbeReport(++phase,1);
            File.Delete("relative.bin"); File.Delete("moved.bin");
            Environment.CurrentDirectory=previous;
            Directory.Delete(root);
            Check(!Directory.Exists(root));
            ProbeReport(++phase,1);
            return 0;
        } catch(Exception ex) { ProbeReport(99,phase); ProbeError(ex.ToString()); return 1; }
    }
}
