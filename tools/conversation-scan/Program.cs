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
    public static int Main(string[] args)
    {
        if (args.Length < 2)
        {
            Console.Error.WriteLine("conversation-scan <CookedPCConsole> <out.jsonl> [--lex-dir <Legendary Explorer nightly>] [--only <text>]");
            Console.Error.WriteLine("conversation-scan <CookedPCConsole> <out.jsonl> --wheels [--only <file text>] [--isb <ISACT folder>]  every line as text (Thespian mode, research/70)");
            Console.Error.WriteLine("conversation-scan <ISACT folder> <out folder> --vo-extract <list.tsv>  lines' recordings as WAV (Thespian mode's bench, research/70)");
            Console.Error.WriteLine("conversation-scan <CookedPCConsole> <out.txt> --state-events <id,id,...>   what those state transitions do");
            Console.Error.WriteLine("conversation-scan <CookedPCConsole> <out.txt> --references <name,name,...>  which level objects point at those conversations");
            Console.Error.WriteLine("conversation-scan <CookedPCConsole> <out.txt> --class-dump <Class> [--only <file text>]  every object of a class, its properties");
            Console.Error.WriteLine("conversation-scan <CookedPCConsole> <out.txt> --script <Class.Function> [--only <file text>]  a script function or state, decompiled (game code: never commit it)");
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
            return StateEvents.Run(args[0], args[1], stateEvents);
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
