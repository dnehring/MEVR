// SPDX-License-Identifier: GPL-3.0-only
// What a state transition does (research/69 item 1): `conversation-scan <CookedPCConsole> <out.txt> --state-events 188,189`
// finds the game's BioStateEventMap exports (the plot manager's state transition maps), reads them through Legendary
// Explorer's binary converter and prints each asked event's elements (set a plot bool, int or float, call a function, a
// consequence), member by member through reflection, so the tool doesn't depend on the converter's member names.
// Reads only. Its output is game data: keep it out of the repository.
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;
using LegendaryExplorerCore;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.Unreal.BinaryConverters;

namespace Mevr.ConversationScan;

internal static class StateEvents
{
    public static int Run(string cooked, string outPath, string idList)
    {
        LegendaryExplorerCoreLib.InitLib(TaskScheduler.Default, s => Console.Error.WriteLine("LEC: " + s), null, new[] { MEGame.LE1 });
        var wanted = new HashSet<int>(idList.Split(',', StringSplitOptions.RemoveEmptyEntries).Select(int.Parse));
        using var writer = new StreamWriter(outPath);
        int maps = 0, found = 0;
        foreach (string file in Directory.GetFiles(cooked, "*.pcc").OrderBy(f => f))
        {
            // LE1 keeps them in BIOC_Materials (PlotManagerAuto.StateTransitionMap, 3,751 events, and its ConsequenceMap) and in
            // PlotManagerAutoDLC_UNC (found by opening every package once, 2026-11-09).
            string name = Path.GetFileNameWithoutExtension(file);
            if (!name.Equals("BIOC_Materials", StringComparison.OrdinalIgnoreCase) && !name.Contains("PlotManager", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }
            try
            {
                using IMEPackage pcc = MEPackageHandler.OpenMEPackage(file, forceLoadFromDisk: true);
                foreach (ExportEntry export in pcc.Exports.Where(e => e.ClassName == "BioStateEventMap"))
                {
                    ++maps;
                    object map = ObjectBinary.From<BioStateEventMap>(export);
                    Console.Error.WriteLine($"map {name} {export.InstancedFullPath}: {(Member(map, "StateEvents") as System.Collections.ICollection)?.Count} events, {export.DataSize} bytes");
                    foreach (object ev in Children(map))
                    {
                        object id = Member(ev, "ID");
                        if (id == null || !wanted.Contains(Convert.ToInt32(id)))
                        {
                            continue;
                        }
                        ++found;
                        writer.WriteLine($"event {id} in {name} {export.InstancedFullPath}");
                        foreach (object element in Children(ev))
                        {
                            writer.WriteLine("    " + element.GetType().Name + ": " + Describe(element));
                        }
                    }
                }
            }
            catch (Exception e)
            {
                Console.Error.WriteLine($"{name}: {e.GetType().Name}: {e.Message}");
            }
        }
        Console.Error.WriteLine($"{maps} state event maps read, {found} events found -> {outPath}");
        return 0;
    }

    // The first member of `o` that is a list of objects (the map's events, an event's elements).
    private static IEnumerable<object> Children(object o)
    {
        foreach (MemberInfo m in Members(o))
        {
            object v = Value(o, m);
            if (v is IEnumerable list && v is not string)
            {
                var items = list.Cast<object>().ToList();
                if (items.Count > 0 && !items[0].GetType().IsPrimitive)
                {
                    return items;
                }
            }
        }
        return Array.Empty<object>();
    }

    private static object Member(object o, string name)
    {
        MemberInfo m = Members(o).FirstOrDefault(x => string.Equals(x.Name, name, StringComparison.OrdinalIgnoreCase));
        return m == null ? null : Value(o, m);
    }

    private static string Describe(object o)
    {
        return string.Join(", ", Members(o).Select(m => (m.Name, Value: Value(o, m)))
            .Where(p => p.Value != null && (p.Value.GetType().IsPrimitive || p.Value is string || p.Value.GetType().IsEnum))
            .Select(p => $"{p.Name}={p.Value}"));
    }

    private static IEnumerable<MemberInfo> Members(object o)
    {
        const BindingFlags flags = BindingFlags.Public | BindingFlags.Instance;
        return o.GetType().GetFields(flags).Cast<MemberInfo>().Concat(o.GetType().GetProperties(flags).Where(p => p.GetIndexParameters().Length == 0));
    }

    private static object Value(object o, MemberInfo m)
    {
        try
        {
            return m is FieldInfo f ? f.GetValue(o) : ((System.Reflection.PropertyInfo)m).GetValue(o);
        }
        catch
        {
            return null;
        }
    }
}
