using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;
using System.Threading.Tasks;

namespace Helix.Scripting
{
    /// <summary>The native side's table (the same layout as in DotnetHost.cpp).</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct NativeApi
    {
        public uint size;
        public uint version;
        public delegate* unmanaged[Cdecl]<int, byte*, int, void> log;
        public delegate* unmanaged[Cdecl]<byte*, int, void> setError;
        public delegate* unmanaged[Cdecl]<byte*, int, void*> findExport;
        public delegate* unmanaged[Cdecl]<byte*, int, uint*, void**> nativeFunctions;
    }

    /// <summary>The managed side's table, filled by Initialize.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ManagedApi
    {
        public uint size;
        public uint version;
        public delegate* unmanaged[Cdecl]<void> shutdown;
        public delegate* unmanaged[Cdecl]<byte*, int, ulong*, int> loadAssembly;
        public delegate* unmanaged[Cdecl]<ulong, byte*, int, byte*, int, void**, int> getFunction;
        public delegate* unmanaged[Cdecl]<ulong, ulong> retainHandle;
        public delegate* unmanaged[Cdecl]<ulong, void> releaseHandle;
        public delegate* unmanaged[Cdecl]<ulong, ulong> weakHandle;
        public delegate* unmanaged[Cdecl]<ulong, byte> handleAlive;
        public delegate* unmanaged[Cdecl]<void> collect;
    }

    /// <summary>The levels of the native log (Helix's LogLevel).</summary>
    public enum LogLevel
    {
        Trace,
        Debug,
        Info,
        Warning,
        Error,
        Fatal,
    }

    /// <summary>
    /// The managed side of Helix's .NET host: the entry points native code calls (each catches what it throws and
    /// reports it through the native log), the engine's load context, and what managed code uses of the native side
    /// (the log, native exports and function tables, GC handles for objects native code keeps).
    /// </summary>
    public static unsafe class Host
    {
        static NativeApi s_Native;
        static EngineLoadContext s_Engine;
        static readonly List<Assembly> s_Assemblies = new();
        static bool s_Hooked;

        /// <summary>The engine's load context (null while the host is stopped).</summary>
        public static AssemblyLoadContext Engine => s_Engine;

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static int Initialize(NativeApi* native, ManagedApi* managed)
        {
            try
            {
                if (native->size != (uint)sizeof(NativeApi))
                    return 2;
                s_Native = *native;
                s_Engine = new EngineLoadContext();
                s_Assemblies.Clear();
                if (!s_Hooked)
                {
                    s_Hooked = true;
                    AppDomain.CurrentDomain.UnhandledException += (_, e) => Log(LogLevel.Fatal, "an unhandled exception: " + e.ExceptionObject);
                    TaskScheduler.UnobservedTaskException += (_, e) =>
                    {
                        Report(e.Exception);
                        e.SetObserved();
                    };
                }
                *managed = default;
                managed->size = (uint)sizeof(ManagedApi);
                managed->version = 1;
                managed->shutdown = &Shutdown;
                managed->loadAssembly = &LoadAssembly;
                managed->getFunction = &GetFunction;
                managed->retainHandle = &RetainHandle;
                managed->releaseHandle = &ReleaseHandle;
                managed->weakHandle = &WeakHandle;
                managed->handleAlive = &HandleAlive;
                managed->collect = &Collect;
                Log(LogLevel.Info, $"started on {RuntimeInformation.FrameworkDescription}");
                return 0;
            }
            catch (Exception e)
            {
                SetError(e.ToString());
                return 1;
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        static void Shutdown()
        {
            try
            {
                s_Assemblies.Clear();
                s_Engine = null;
                GC.Collect();
                GC.WaitForPendingFinalizers();
            }
            catch (Exception e)
            {
                Report(e);
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        static int LoadAssembly(byte* path, int size, ulong* handle)
        {
            try
            {
                Assembly a = LoadEngineAssembly(Encoding.UTF8.GetString(path, size));
                int index = s_Assemblies.IndexOf(a);
                if (index < 0)
                {
                    s_Assemblies.Add(a);
                    index = s_Assemblies.Count - 1;
                }
                *handle = (ulong)index + 1;
                return 0;
            }
            catch (Exception e)
            {
                SetError(e.GetType().Name + ": " + e.Message);
                return 1;
            }
        }

        /// <summary>
        /// An assembly by its path into the engine's load context (its folder joins the folders the context resolves
        /// from); on the Web an assembly the app carries is taken by its name.
        /// </summary>
        public static Assembly LoadEngineAssembly(string path)
        {
            string file = Path.GetFullPath(path);
            Assembly a = AppAssembly(file);
            if (a != null)
                return a;
            s_Engine.AddFolder(Path.GetDirectoryName(file));
            return s_Engine.LoadFromAssemblyPath(file);
        }

        // The Web's apps carry the engine's assemblies (built with them, so their native calls have wrappers): an
        // assembly of the app is taken by name from the default context; elsewhere null.
        static Assembly AppAssembly(string file)
        {
            if (!OperatingSystem.IsBrowser())
                return null;
            try
            {
                return AssemblyLoadContext.Default.LoadFromAssemblyName(new AssemblyName(Path.GetFileNameWithoutExtension(file)));
            }
            catch (FileNotFoundException)
            {
                return null;
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        static int GetFunction(ulong assembly, byte* type, int typeSize, byte* method, int methodSize, void** function)
        {
            try
            {
                *function = null;
                if (assembly == 0 || assembly > (ulong)s_Assemblies.Count)
                {
                    SetError("not an assembly of the engine's load context");
                    return 1;
                }
                Assembly a = s_Assemblies[(int)assembly - 1];
                string typeName = Encoding.UTF8.GetString(type, typeSize);
                string methodName = Encoding.UTF8.GetString(method, methodSize);
                Type t = a.GetType(typeName);
                if (t == null)
                {
                    SetError($"{a.GetName().Name} has no type {typeName}");
                    return 1;
                }
                MethodInfo m = t.GetMethod(methodName, BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic);
                if (m == null)
                {
                    SetError($"{typeName} has no static method {methodName}");
                    return 1;
                }
                if (m.GetCustomAttribute<UnmanagedCallersOnlyAttribute>() == null)
                {
                    SetError($"{typeName}.{methodName} is not marked UnmanagedCallersOnly");
                    return 1;
                }
                *function = (void*)m.MethodHandle.GetFunctionPointer();
                return 0;
            }
            catch (Exception e)
            {
                SetError(e.GetType().Name + ": " + e.Message);
                return 1;
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        static ulong RetainHandle(ulong handle)
        {
            try
            {
                object target = GCHandle.FromIntPtr((IntPtr)handle).Target;
                return target == null ? 0 : ToNative(target);
            }
            catch (Exception e)
            {
                Report(e);
                return 0;
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        static void ReleaseHandle(ulong handle)
        {
            try
            {
                GCHandle.FromIntPtr((IntPtr)handle).Free();
            }
            catch (Exception e)
            {
                Report(e);
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        static ulong WeakHandle(ulong handle)
        {
            try
            {
                object target = GCHandle.FromIntPtr((IntPtr)handle).Target;
                return target == null ? 0 : (ulong)GCHandle.ToIntPtr(GCHandle.Alloc(target, GCHandleType.Weak));
            }
            catch (Exception e)
            {
                Report(e);
                return 0;
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        static byte HandleAlive(ulong handle)
        {
            try
            {
                return GCHandle.FromIntPtr((IntPtr)handle).Target != null ? (byte)1 : (byte)0;
            }
            catch (Exception e)
            {
                Report(e);
                return 0;
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        static void Collect()
        {
            try
            {
                GC.Collect();
                GC.WaitForPendingFinalizers();
                GC.Collect();
            }
            catch (Exception e)
            {
                Report(e);
            }
        }

        // ------------------------------------------------------------------------------------------- for managed code

        /// <summary>A GC handle of an object for native code to keep (0 for null); native code releases it.</summary>
        public static ulong ToNative(object target) => target == null ? 0 : (ulong)GCHandle.ToIntPtr(GCHandle.Alloc(target));

        /// <summary>The object of a GC handle native code holds.</summary>
        public static object FromNative(ulong handle) => handle == 0 ? null : GCHandle.FromIntPtr((IntPtr)handle).Target;

        public static void Log(LogLevel level, string text)
        {
            if (s_Native.log == null)
            {
                Console.Error.WriteLine(text);
                return;
            }
            byte[] bytes = Encoding.UTF8.GetBytes(text ?? "");
            fixed (byte* p = bytes)
                s_Native.log((int)level, p, bytes.Length);
        }

        /// <summary>An exception that must not cross into native code: logged as an error with its stack.</summary>
        public static void Report(Exception e) => Log(LogLevel.Error, e.ToString());

        /// <summary>A function native code registered by name (RegisterNativeExport), or null.</summary>
        public static void* FindExport(string name)
        {
            if (s_Native.findExport == null)
                return null;
            byte[] bytes = Encoding.UTF8.GetBytes(name);
            fixed (byte* p = bytes)
                return s_Native.findExport(p, bytes.Length);
        }

        /// <summary>The function table a native module registered (RegisterNativeFunctions).</summary>
        public static ReadOnlySpan<IntPtr> NativeFunctions(string module)
        {
            if (s_Native.nativeFunctions == null)
                return default;
            byte[] bytes = Encoding.UTF8.GetBytes(module);
            uint count;
            void** table;
            fixed (byte* p = bytes)
                table = s_Native.nativeFunctions(p, bytes.Length, &count);
            return table == null ? default : new ReadOnlySpan<IntPtr>(table, (int)count);
        }

        static void SetError(string text)
        {
            Log(LogLevel.Error, text);
            if (s_Native.setError == null)
                return;
            byte[] bytes = Encoding.UTF8.GetBytes(text);
            fixed (byte* p = bytes)
                s_Native.setError(p, bytes.Length);
        }
    }

    /// <summary>
    /// The engine's load context: engine and script assemblies, resolved from the folders of the assemblies loaded
    /// into it; the runtime's own assembly and the framework stay in the default context.
    /// </summary>
    sealed class EngineLoadContext : AssemblyLoadContext
    {
        readonly List<string> _folders = new();

        public EngineLoadContext() : base("HelixEngine", isCollectible: false) { }

        public void AddFolder(string folder)
        {
            if (folder != null && !_folders.Contains(folder))
                _folders.Add(folder);
        }

        protected override Assembly Load(AssemblyName name)
        {
            if (name.Name == typeof(Host).Assembly.GetName().Name)
                return null;
            // On the Web the app's own assemblies stay in the default context.
            if (OperatingSystem.IsBrowser())
            {
                try
                {
                    return Default.LoadFromAssemblyName(name);
                }
                catch (FileNotFoundException)
                {
                }
            }
            foreach (string folder in _folders)
            {
                string path = Path.Combine(folder, name.Name + ".dll");
                if (File.Exists(path))
                    return LoadFromAssemblyPath(path);
            }
            return null;
        }
    }
}
