// SPDX-License-Identifier: GPL-3.0-only
// The conversation scanner (research/59 "The scan"): finds Legendary Explorer's assemblies, then scans.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace Mevr.ConversationScan;

internal static class Program
{
    internal static string GameName { get; private set; } = "LE1";

    public static int Main(string[] args)
    {
        if (args.Length < 2)
        {
            Console.Error.WriteLine("conversation-scan <CookedPCConsole> <out.jsonl> [--game LE1|LE2] [--lex-dir <Legendary Explorer nightly>] [--only <text>]");
            Console.Error.WriteLine("conversation-scan <CookedPCConsole> <out.jsonl> --wheels [--only <file text>] [--isb <ISACT folder>]  every line as text (Thespian mode, research/70)");
            Console.Error.WriteLine("conversation-scan <ISACT folder> <out folder> --vo-extract <list.tsv>  lines' recordings as WAV (Thespian mode's bench, research/70)");
            Console.Error.WriteLine("conversation-scan <CookedPCConsole> <out> --state-events <id,id,...> [--game LE2 --only <package>]   direct serialized state-transition elements");
            Console.Error.WriteLine("conversation-scan <CookedPCConsole> <out.txt> --references <name,name,...>  which level objects point at those conversations");
            Console.Error.WriteLine("conversation-scan <CookedPCConsole> <out.txt> --class-dump <Class> [--only <file text>]  every object of a class, its properties");
            Console.Error.WriteLine("conversation-scan <CookedPCConsole> <out.txt> --script <Class.Function> [--only <file text>]  a script function or state, decompiled (game code: never commit it)");
            return 2;
        }
        int gameIndex = Array.IndexOf(args, "--game");
        if (gameIndex >= 0)
        {
            if (gameIndex + 1 >= args.Length || (args[gameIndex + 1] != "LE1" && args[gameIndex + 1] != "LE2")
                || args.Count(a => a == "--game") != 1)
            {
                Console.Error.WriteLine("--game requires exactly one LE1 or LE2 value.");
                return 2;
            }
            GameName = args[gameIndex + 1];
        }
        if (GameName == "LE2" && args.Any(a => a is "--wheels" or "--vo-extract" or "--references"))
        {
            Console.Error.WriteLine("LE2 wheels/audio and reference search are not implemented; use the read-only scan, class, script, sequence or scoped state-event inspection modes.");
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
        // In its own method: nothing of LEC may be touched before the resolver above is up.
        try
        {
            return Dispatch(args);
        }
        catch (Exception e)
        {
            Console.Error.WriteLine($"{e.GetType().Name}: {e.Message}");
            return 1;
        }
    }

    private static int Dispatch(string[] args)
    {
        if (args.Contains("--self-test"))
        {
            return InspectionTests.Run();
        }
        if (GameName == "LE2")
        {
            string source = Path.GetFullPath(args[0]).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string output = Path.GetFullPath(args[1]);
            if (File.Exists(output) || Directory.Exists(output)
                || output.Equals(source, StringComparison.OrdinalIgnoreCase)
                || output.StartsWith(source + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                throw new IOException("LE2 inspection requires a new output file outside the input package directory.");
        }
        string voExtract = args.SkipWhile(a => a != "--vo-extract").Skip(1).FirstOrDefault();
        if (voExtract != null)
        {
            return VoExtract.Run(args[0], args[1], voExtract);
        }
        if (args.Contains("--wheels"))
        {
            return Wheels.Run(args[0], args[1], args.SkipWhile(a => a != "--only").Skip(1).FirstOrDefault(),
                args.SkipWhile(a => a != "--isb").Skip(1).FirstOrDefault());
        }
        string stateEvents = args.SkipWhile(a => a != "--state-events").Skip(1).FirstOrDefault();
        if (stateEvents != null)
        {
            return StateEvents.Run(args[0], args[1], stateEvents, args.SkipWhile(a => a != "--only").Skip(1).FirstOrDefault());
        }
        string sequence = args.SkipWhile(a => a != "--sequence").Skip(1).FirstOrDefault();
        if (sequence != null)
        {
            return SequenceDump.Run(args[0], args[1], sequence);
        }
        string script = args.SkipWhile(a => a != "--script").Skip(1).FirstOrDefault();
        if (script != null)
        {
            return ScriptDump.Run(args[0], args[1], script, args.SkipWhile(a => a != "--only").Skip(1).FirstOrDefault());
        }
        string classDump = args.SkipWhile(a => a != "--class-dump").Skip(1).FirstOrDefault();
        if (classDump != null)
        {
            return ClassDump.Run(args[0], args[1], classDump, args.SkipWhile(a => a != "--only").Skip(1).FirstOrDefault());
        }
        string references = args.SkipWhile(a => a != "--references").Skip(1).FirstOrDefault();
        if (references != null)
        {
            return References.Run(args[0], args[1], references);
        }
        return Scan.Run(args[0], args[1], args.SkipWhile(a => a != "--only").Skip(1).FirstOrDefault());
    }
}
