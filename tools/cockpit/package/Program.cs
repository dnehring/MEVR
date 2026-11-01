// SPDX-License-Identifier: GPL-3.0-only
// The cockpit packager's entry point: finds Legendary Explorer's assemblies, then runs a verb (research/47 "Packaging").
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace Mevr.CockpitPackage;

internal static class Program
{
    private const string Usage = """
        cockpit-package: our cockpit .glb models into one LE1 package (research/47).

          build   --assets <concept/assets> [--out <file.pcc>] [--max-texture N] [--repo <repo root>]
                  Default out: <repo>/build/cockpit/BIOG_MEVR_Cockpit.pcc, with <out>.json listing the objects.
          check   <file.pcc> [--assets <concept/assets>]
                  Reopens a package and validates it (the build runs this at its end).
          inspect <file.pcc> [--filter text] [--props] [--bin]
                  Lists exports and imports; with --props each one's properties.
          toc     --toc <PCConsoleTOC.bin> --out <new TOC> (--add <file.pcc> [--as <relative path>] | --remove <file name>) | --list <text>
                  Writes a copy of a TOC with one entry added or removed. Never writes over its input.
          survey  <CookedPCConsole> [--pattern BIOA_*_ART.pcc] [--count N]
                  What the game's own meshes, textures and instances hold: the reference for what the build writes.

        Options for all: --lex-dir <folder> (Legendary Explorer nightly), --game <LE1 game folder, for reading its packages>
        """;

    public static int Main(string[] args)
    {
        Options options;
        try
        {
            options = Options.Parse(args);
        }
        catch (ArgumentException e)
        {
            Console.Error.WriteLine(e.Message);
            Console.Error.WriteLine(Usage);
            return 2;
        }
        if (options.Verb == null)
        {
            Console.Error.WriteLine(Usage);
            return 2;
        }
        string lexDir = options.Get("lex-dir")
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
        // In its own method: nothing of LEC may be touched before the resolver above is up.
        return Verbs.Run(options);
    }
}

internal sealed class Options
{
    public string Verb;
    public List<string> Positional = new();
    private readonly Dictionary<string, string> named = new(StringComparer.OrdinalIgnoreCase);
    private static readonly HashSet<string> Flags = new(StringComparer.OrdinalIgnoreCase) { "props", "bin" };

    public string Get(string name) => named.TryGetValue(name, out string value) ? value : null;
    public bool Has(string name) => named.ContainsKey(name);

    public static Options Parse(string[] args)
    {
        var options = new Options();
        for (int i = 0; i < args.Length; ++i)
        {
            string arg = args[i];
            if (arg.StartsWith("--", StringComparison.Ordinal))
            {
                string name = arg[2..];
                if (Flags.Contains(name))
                {
                    options.named[name] = "1";
                }
                else if (i + 1 < args.Length)
                {
                    options.named[name] = args[++i];
                }
                else
                {
                    throw new ArgumentException($"{arg} needs a value");
                }
            }
            else if (options.Verb == null)
            {
                options.Verb = arg;
            }
            else
            {
                options.Positional.Add(arg);
            }
        }
        return options;
    }
}
