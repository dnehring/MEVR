// SPDX-License-Identifier: GPL-3.0-only
// tools/rig/README.md: adds bones to a character's skeletal mesh from the player's own game files (hair chains, soft-tissue
// bones) and weights the mesh to them, writing a package of ours. Uses Legendary Explorer's library (GPL-3.0). Everything
// it reads or writes is the player's game data: it stays local, never committed or shipped.
using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Threading.Tasks;

namespace Mevr.Rig;

internal static class Program
{
    public static int Main(string[] args)
    {
        if (args.Length < 1)
        {
            Console.Error.WriteLine("mevr-rig inspect <package.pcc> <mesh path, or its end> [--bones] [--game LE1|LE2|LE3]");
            Console.Error.WriteLine("mevr-rig rig <plan.json> [--game LE3] [--out <folder>]   (tools/rig/README.md)");
            Console.Error.WriteLine("mevr-rig bodies --game LE1|LE2|LE3 [--game-root <folder>] [--out <folder>] [--ref <ME3 bodies package>]");
            return 2;
        }
        // The players' build (Rig.csproj -p:MevrPlayers=true, MEVR Setup's payload\rig) carries Legendary Explorer's library next
        // to the exe; a development build finds it in Legendary Explorer's own folder.
        string lexDir = Arg(args, "--lex-dir")
            ?? Environment.GetEnvironmentVariable("MEVR_LEX_DIR")
            ?? (File.Exists(Path.Combine(AppContext.BaseDirectory, "LegendaryExplorerCore.dll")) ? AppContext.BaseDirectory : null)
            ?? typeof(Program).Assembly.GetCustomAttributes<AssemblyMetadataAttribute>().FirstOrDefault(a => a.Key == "LexDir")?.Value;
        if (lexDir == null || !File.Exists(Path.Combine(lexDir, "LegendaryExplorerCore.dll")))
        {
            Console.Error.WriteLine($"No LegendaryExplorerCore.dll in '{lexDir}': pass --lex-dir.");
            return 2;
        }
        lexDir = Path.GetFullPath(lexDir);
        AssemblyLoadContext.Default.Resolving += (context, name) =>
        {
            string path = Path.Combine(lexDir, name.Name + ".dll");
            return File.Exists(path) ? context.LoadFromAssemblyPath(path) : null;
        };
        AssemblyLoadContext.Default.ResolvingUnmanagedDll += (assembly, name) =>
        {
            foreach (string folder in new[] { lexDir, Path.Combine(lexDir, "runtimes", "win-x64", "native") })
            {
                foreach (string candidate in new[] { name, name + ".dll" })
                {
                    string path = Path.Combine(folder, candidate);
                    if (File.Exists(path))
                    {
                        return NativeLibrary.Load(path);
                    }
                }
            }
            return IntPtr.Zero;
        };
        try
        {
            return Dispatch(args);  // nothing of Legendary Explorer is touched before the resolver above is up
        }
        catch (Exception e)
        {
            Console.Error.WriteLine($"{e.GetType().Name}: {e.Message}\n{e.StackTrace}");
            return 1;
        }
        finally
        {
            // MEVR_RIG_LOADED=1: which assemblies and native modules the run loaded (what the players' build must carry:
            // Rig.csproj's LexRuntime list, tools/rig/README.md "The players' build").
            if (Environment.GetEnvironmentVariable("MEVR_RIG_LOADED") == "1")
            {
                foreach (var a in AppDomain.CurrentDomain.GetAssemblies().Select(a => a.GetName().Name).OrderBy(n => n))
                {
                    Console.Error.WriteLine("loaded assembly: " + a);
                }
                foreach (System.Diagnostics.ProcessModule m in System.Diagnostics.Process.GetCurrentProcess().Modules)
                {
                    Console.Error.WriteLine("loaded module: " + m.FileName);
                }
            }
        }
    }

    // Its own method, never inlined: compiling it loads Legendary Explorer's types, which needs the resolver.
    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    private static int Dispatch(string[] args)
    {
        string game = Arg(args, "--game") ?? "LE3";
        // `bodies` for ME1 or ME2 may build ME3's reference body for the sizes (Bodies.cs).
        Lex.Init(game, Rigging.GameRoot(args), args[0] == "bodies" ? new[] { "LE3" } : Array.Empty<string>());
        return args[0] switch
        {
            "bodies" => Bodies.Run(args),
            "inspect" => Inspect.Run(args),
            "rig" => Rigging.Run(args),
            "contents" => Contents.Run(args),
            "find" => Contents.Find(args),
            "colliders" => Colliders.Run(args),
            "place" => Placement.Run(args),
            "census" => Census.Run(args),
            "sizes" => Sizes.Run(args),
            "export" => Sizes.Export(args),
            "clubs" => Clubs.Run(args),
            _ => Usage(),
        };
    }

    private static int Usage()
    {
        Console.Error.WriteLine("unknown verb: inspect | rig | bodies | contents | find | colliders | place | census | sizes | export | clubs");
        return 2;
    }

    public static string Arg(string[] args, string name) => args.SkipWhile(a => a != name).Skip(1).FirstOrDefault();
}
