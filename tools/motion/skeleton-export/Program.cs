// SPDX-License-Identifier: GPL-3.0-only
// tools/motion/README.md: a skeletal mesh's reference skeleton, one bone per line (index, name, parent, local position xyz in
// game units, local rotation quaternion xyzw as the package stores it), for tools/motion/ardy-clip.py's retarget. The output
// is the player's game data: it stays local (never committed or shipped).
using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Threading.Tasks;

namespace Mevr.SkeletonExport;

internal static class Program
{
    public static int Main(string[] args)
    {
        if (args.Length < 3)
        {
            Console.Error.WriteLine("skeleton-export <package.pcc> <mesh path, or its end> <out.tsv> [--game LE1|LE2|LE3] [--lex-dir <Legendary Explorer nightly>]");
            Console.Error.WriteLine("  e.g. skeleton-export BioD_CitHub_Miranda.pcc HMF_ARM_MIRa_MDL miranda.tsv --game LE3");
            return 2;
        }
        string lexDir = args.SkipWhile(a => a != "--lex-dir").Skip(1).FirstOrDefault()
            ?? Environment.GetEnvironmentVariable("MEVR_LEX_DIR")
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
            return Export(args);  // nothing of Legendary Explorer is touched before the resolver above is up
        }
        catch (Exception e)
        {
            Console.Error.WriteLine($"{e.GetType().Name}: {e.Message}");
            return 1;
        }
    }

    private static int Export(string[] args)
    {
        string game = args.SkipWhile(a => a != "--game").Skip(1).FirstOrDefault() ?? "LE3";
        var games = game switch
        {
            "LE1" => new[] { LegendaryExplorerCore.Packages.MEGame.LE1 },
            "LE2" => new[] { LegendaryExplorerCore.Packages.MEGame.LE2 },
            _ => new[] { LegendaryExplorerCore.Packages.MEGame.LE3 },
        };
        LegendaryExplorerCore.LegendaryExplorerCoreLib.InitLib(TaskScheduler.Default, _ => { }, null, games);
        using var package = LegendaryExplorerCore.Packages.MEPackageHandler.OpenMEPackage(args[0], forceLoadFromDisk: true);
        var export = package.Exports.FirstOrDefault(e => e.ClassName == "SkeletalMesh" &&
            e.InstancedFullPath.EndsWith(args[1], StringComparison.OrdinalIgnoreCase));
        if (export == null)
        {
            Console.Error.WriteLine($"No SkeletalMesh ending '{args[1]}' in {args[0]}");
            return 1;
        }
        var mesh = LegendaryExplorerCore.Unreal.BinaryConverters.ObjectBinary.From<LegendaryExplorerCore.Unreal.BinaryConverters.SkeletalMesh>(export);
        using var writer = new StreamWriter(args[2]);
        writer.WriteLine($"# {export.InstancedFullPath} from {Path.GetFileName(args[0])} ({game}): index, name, parent, position xyz, rotation xyzw");
        for (int i = 0; i < mesh.RefSkeleton.Length; i++)
        {
            var bone = mesh.RefSkeleton[i];
            int parent = bone.ParentIndex == i ? -1 : bone.ParentIndex;
            var t = bone.Position;
            var q = bone.Orientation;
            writer.WriteLine(string.Join('\t', i, bone.Name.Instanced, parent, t.X.ToString("R"), t.Y.ToString("R"), t.Z.ToString("R"),
                q.X.ToString("R"), q.Y.ToString("R"), q.Z.ToString("R"), q.W.ToString("R")));
        }
        Console.WriteLine($"{mesh.RefSkeleton.Length} bones of {export.InstancedFullPath} -> {args[2]}");
        return 0;
    }
}
