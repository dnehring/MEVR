// SPDX-License-Identifier: GPL-3.0-only
// One script function or state as Legendary Explorer's decompiler reads it (research/69 "Walking to marks"):
// `conversation-scan <CookedPCConsole> <out.txt> --script <Class.Function | grep:text> [--only <file text>]` writes the decompiled text of
// every function, state or enum of that name under that class in the game's packages (the first package that has it, normally
// SFXGame.pcc or Engine.pcc). For reading why the game refuses something, not for copying: the output is the game's code,
// keep it out of the repository and write findings in words.
using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using LegendaryExplorerCore;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.UnrealScript;

namespace Mevr.ConversationScan;

internal static class ScriptDump
{
    public static int Run(string cooked, string outPath, string target, string only)
    {
        LegendaryExplorerCoreLib.InitLib(TaskScheduler.Default, s => Console.Error.WriteLine("LEC: " + s), null, new[] { MEGame.LE1 });
        if (target == "exports")
        {
            return Exports(cooked, outPath, only ?? "SFXGame");
        }
        if (target.StartsWith("grep:", StringComparison.Ordinal))
        {
            return Grep(cooked, outPath, target.Substring(5), only ?? "SFXGame");
        }
        string[] parts = target.Split('.');
        if (parts.Length != 2)
        {
            Console.Error.WriteLine("--script wants Class.Function");
            return 2;
        }
        string cls = parts[0], fn = parts[1];
        string[] files = Directory.GetFiles(cooked, "*.pcc", SearchOption.AllDirectories)
            .Where(f => only == null || Path.GetFileName(f).Contains(only, StringComparison.OrdinalIgnoreCase))
            .OrderBy(f => Path.GetFileName(f).StartsWith("SFXGame", StringComparison.OrdinalIgnoreCase) ? 0 : Path.GetFileName(f).StartsWith("Engine", StringComparison.OrdinalIgnoreCase) ? 1 : 2)
            .ToArray();
        using var writer = new StreamWriter(outPath);
        int found = 0;
        foreach (string file in files)
        {
            using IMEPackage pcc = MEPackageHandler.OpenMEPackage(file, forceLoadFromDisk: true);
            foreach (ExportEntry export in pcc.Exports.Where(e => (e.ClassName == "Function" || e.ClassName == "State" || e.ClassName == "Enum") &&
                         e.ObjectName.Instanced.Equals(fn, StringComparison.OrdinalIgnoreCase) && e.Parent != null &&
                         e.Parent.ObjectName.Instanced.Equals(cls, StringComparison.OrdinalIgnoreCase)))
            {
                var options = new UnrealScriptOptionsPackage();
                var lib = new FileLib(pcc);
                bool ready = lib.Initialize(options);
                (_, string text) = UnrealScriptCompiler.DecompileExport(export, ready ? lib : null, options);
                writer.WriteLine($"// {Path.GetFileName(file)}: {export.InstancedFullPath} ({export.ClassName})");
                writer.WriteLine(text);
                writer.WriteLine();
                found++;
            }
            if (found > 0)
            {
                break;
            }
        }
        Console.WriteLine($"{found} found for {target} -> {outPath}");
        return found > 0 ? 0 : 1;
    }

    // `--script exports --only <file text>`: what a package holds, by class (the count and the first names of each), for finding
    // where the game keeps something (LE1's conversation conditionals, research/69 item 12).
    static int Exports(string cooked, string outPath, string only)
    {
        string[] files = Directory.GetFiles(cooked, "*.pcc", SearchOption.AllDirectories)
            .Where(f => Path.GetFileName(f).Contains(only, StringComparison.OrdinalIgnoreCase)).ToArray();
        using var writer = new StreamWriter(outPath);
        foreach (string file in files)
        {
            using IMEPackage pcc = MEPackageHandler.OpenMEPackage(file, forceLoadFromDisk: true);
            writer.WriteLine($"// {Path.GetFileName(file)}: {pcc.ExportCount} exports");
            foreach (var group in pcc.Exports.GroupBy(e => e.ClassName).OrderByDescending(g => g.Count()))
            {
                writer.WriteLine($"{group.Key}: {group.Count()} ({string.Join(", ", group.Take(6).Select(e => e.InstancedFullPath))})");
            }
        }
        Console.WriteLine($"{files.Length} packages listed -> {outPath}");
        return files.Length > 0 ? 0 : 1;
    }

    // `--script grep:<text> [--only <file text>]`: every function and state of the packages whose name holds the file text
    // (SFXGame by default) whose decompiled text holds <text>: who calls something.
    static int Grep(string cooked, string outPath, string text, string only)
    {
        string[] files = Directory.GetFiles(cooked, "*.pcc", SearchOption.AllDirectories)
            .Where(f => Path.GetFileName(f).Contains(only, StringComparison.OrdinalIgnoreCase)).ToArray();
        using var writer = new StreamWriter(outPath);
        int found = 0, read = 0;
        foreach (string file in files)
        {
            using IMEPackage pcc = MEPackageHandler.OpenMEPackage(file, forceLoadFromDisk: true);
            var options = new UnrealScriptOptionsPackage();
            var lib = new FileLib(pcc);
            bool ready = lib.Initialize(options);
            foreach (ExportEntry export in pcc.Exports.Where(e => e.ClassName == "Function" || e.ClassName == "State"))
            {
                read++;
                string body;
                try
                {
                    (_, body) = UnrealScriptCompiler.DecompileExport(export, ready ? lib : null, options);
                }
                catch (Exception)
                {
                    continue;
                }
                if (body == null || !body.Contains(text, StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }
                writer.WriteLine($"// {Path.GetFileName(file)}: {export.InstancedFullPath} ({export.ClassName})");
                writer.WriteLine(body);
                writer.WriteLine();
                found++;
            }
        }
        Console.WriteLine($"{found} of {read} functions and states hold '{text}' -> {outPath}");
        return found > 0 ? 0 : 1;
    }
}
