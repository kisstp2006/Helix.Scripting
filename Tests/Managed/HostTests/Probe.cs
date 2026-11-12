using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace Helix.Scripting.HostTests
{
    sealed class Box
    {
        public int Value;
    }

    // What the C++ host tests call: both directions, a leaf call, an exception at the boundary, objects native code
    // holds by GC handles, and timings for the benchmarks.
    public static unsafe class Probe
    {
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static int Echo(int value) => value + 1;

        // Calls a native function through a normal and a SuppressGCTransition function pointer.
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static long CallNative(delegate* unmanaged[Cdecl]<int, int> normal, delegate* unmanaged[Cdecl, SuppressGCTransition]<int, int> leaf, int count)
        {
            long sum = 0;
            for (int i = 0; i < count; i++)
                sum += normal(i) + leaf(i);
            return sum;
        }

        // Loops of native calls for the benchmarks (one native to managed call around many the other way).
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static long LoopNative(delegate* unmanaged[Cdecl]<int, int> normal, int count)
        {
            long sum = 0;
            for (int i = 0; i < count; i++)
                sum += normal(i);
            return sum;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static long LoopLeaf(delegate* unmanaged[Cdecl, SuppressGCTransition]<int, int> leaf, int count)
        {
            long sum = 0;
            for (int i = 0; i < count; i++)
                sum += leaf(i);
            return sum;
        }

        // A throw caught at the boundary and reported (with its stack) through the native log.
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static int Throws()
        {
            try
            {
                throw new InvalidOperationException("thrown by Probe");
            }
            catch (Exception e)
            {
                Host.Report(e);
                return -1;
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static int InEngineContext()
        {
            AssemblyLoadContext context = AssemblyLoadContext.GetLoadContext(typeof(Probe).Assembly);
            bool sameRuntime = AssemblyLoadContext.GetLoadContext(typeof(Host).Assembly) == AssemblyLoadContext.Default;
            return context == Host.Engine && context.Name == "HelixEngine" && sameRuntime ? Doubler.Twice(21) : 0;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static ulong MakeBox(int value) => Host.ToNative(new Box { Value = value });

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static int ReadBox(ulong handle) => Host.FromNative(handle) is Box b ? b.Value : -1;

        // A native export by name and a native module's function table, called from C#.
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static int CallExports()
        {
            var add = (delegate* unmanaged[Cdecl]<int, int>)Host.FindExport("HostTests.AddOne");
            ReadOnlySpan<IntPtr> table = Host.NativeFunctions("HostTests");
            if (add == null || table.Length != 1 || Host.FindExport("HostTests.Missing") != null)
                return -1;
            var twice = (delegate* unmanaged[Cdecl]<int, int>)table[0];
            return add(10) + twice(100);
        }
    }

    public static class NotAnEntry
    {
        public static int Plain() => 1;
    }
}
