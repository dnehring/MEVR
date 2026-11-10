// SPDX-License-Identifier: GPL-3.0-only
// Every export of a class with its properties (research/69 "Who turns, and how"): `conversation-scan <CookedPCConsole>
// <out.txt> --class-dump BioLookAtDefinition [--only <file text>]` prints each object of that class in the game's packages,
// structs and arrays spelled out, identical objects counted once. Used to read the look-at chains (which bones turn toward a
// look-at target, how far each, in what order) without the game.
// Reads only. Its output is game data: keep it out of the repository.
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using LegendaryExplorerCore;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.Unreal;

namespace Mevr.ConversationScan;

internal static class ClassDump
{
    private static string Text(IMEPackage pcc, Property p)
    {
        switch (p)
        {
            case ObjectProperty o: return pcc.GetEntry(o.Value)?.InstancedFullPath ?? "none";
            case NameProperty n: return n.Value.Instanced;
            case StrProperty s: return "\"" + s.Value + "\"";
            case IntProperty i: return i.Value.ToString();
            case FloatProperty f: return f.Value.ToString("0.###", System.Globalization.CultureInfo.InvariantCulture);
            case BoolProperty b: return b.Value ? "true" : "false";
            case ByteProperty by: return by.Value.ToString();
            case EnumProperty e: return e.Value.Instanced;
            case StructProperty st: return "{" + string.Join(", ", st.Properties.Where(x => x is not NoneProperty).Select(x => x.Name.Instanced + "=" + Text(pcc, x))) + "}";
            case ArrayPropertyBase a: return "[" + string.Join("; ", a.Properties.Select(x => Text(pcc, x))) + "]";
            default: return p.GetType().Name;
        }
    }

    public static int Run(string cooked, string outPath, string className, string only)
    {
        LegendaryExplorerCoreLib.InitLib(TaskScheduler.Default, s => Console.Error.WriteLine("LEC: " + s), null, new[] { MEGame.LE1 });
        var seen = new ConcurrentDictionary<string, (string first, int count)>();
        string[] files = Directory.GetFiles(cooked, "*.pcc", SearchOption.AllDirectories)
            .Where(f => only == null || Path.GetFileName(f).Contains(only, StringComparison.OrdinalIgnoreCase)).ToArray();
        Parallel.ForEach(files, new ParallelOptions { MaxDegreeOfParallelism = 8 }, file =>
        {
            try
            {
                using IMEPackage pcc = MEPackageHandler.OpenMEPackage(file, forceLoadFromDisk: true);
                foreach (ExportEntry export in pcc.Exports.Where(e => e.ClassName.Equals(className, StringComparison.OrdinalIgnoreCase) && !e.IsDefaultObject))
                {
                    string body = string.Join("\n  ", export.GetProperties().Where(x => x is not NoneProperty).Select(x => x.Name.Instanced + " = " + Text(pcc, x)));
                    string key = export.ObjectName.Instanced + "\n  " + body;
                    seen.AddOrUpdate(key, (Path.GetFileName(file) + ":" + export.InstancedFullPath, 1), (k, v) => (v.first, v.count + 1));
                }
            }
            catch (Exception e)
            {
                Console.Error.WriteLine($"{Path.GetFileName(file)}: {e.Message}");
            }
        });
        using var writer = new StreamWriter(outPath);
        foreach (var pair in seen.OrderByDescending(p => p.Value.count))
        {
            writer.WriteLine($"# {pair.Value.count} x, first {pair.Value.first}");
            writer.WriteLine(pair.Key);
            writer.WriteLine();
        }
        Console.WriteLine($"{seen.Count} distinct {className} objects in {files.Length} packages -> {outPath}");
        return 0;
    }
}
