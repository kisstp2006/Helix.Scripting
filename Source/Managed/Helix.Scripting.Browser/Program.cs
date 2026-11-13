using System;
using System.Runtime.InteropServices;
using System.Text;
using Helix.Scripting;

namespace Helix.Scripting.Browser
{
    /// <summary>
    /// The C# entry of a web app (the .NET browser-wasm runtime with the program linked into its module): hands the
    /// arguments and the managed side's Initialize to the native HxBrowser_Main, which runs the program's main.
    /// </summary>
    public static unsafe class Program
    {
        [DllImport("libhelix_scripting")]
        static extern int HxBrowser_Main(int argc, byte** argv, void* initialize);

        public static int Main(string[] args)
        {
            int argc = args.Length + 1;
            var argv = (byte**)NativeMemory.AllocZeroed((nuint)(argc + 1), (nuint)sizeof(byte*));
            for (int i = 0; i < argc; i++)
            {
                byte[] bytes = Encoding.UTF8.GetBytes(i == 0 ? "HelixApp" : args[i - 1]);
                var text = (byte*)NativeMemory.Alloc((nuint)bytes.Length + 1);
                bytes.AsSpan().CopyTo(new Span<byte>(text, bytes.Length));
                text[bytes.Length] = 0;
                argv[i] = text;
            }
            delegate* unmanaged[Cdecl]<NativeApi*, ManagedApi*, int> initialize = &Host.Initialize;
            int code = HxBrowser_Main(argc, argv, initialize);
            Console.Out.Flush();
            return code;
        }
    }
}
