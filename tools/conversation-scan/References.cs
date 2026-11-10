// SPDX-License-Identifier: GPL-3.0-only
// Who starts a conversation no level script start names (research/69, the sweep): `conversation-scan <CookedPCConsole>
// <out.txt> --references pro20_end_cutscene_matriarch,sp_follower_interject` finds, in every level package that has such a
// conversation among its imports or exports, the objects whose properties point at it: their class, path and the
// property (a matinee's dialogue track, a sequence action of another class, a pawn's own conversation).
// Reads only. Its output is game data: keep it out of the repository.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using LegendaryExplorerCore;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.Unreal;

namespace Mevr.ConversationScan;

internal static class References
{
    public static int Run(string cooked, string outPath, string nameList)
    {
        LegendaryExplorerCoreLib.InitLib(TaskScheduler.Default, s => Console.Error.WriteLine("LEC: " + s), null, new[] { MEGame.LE1 });
        var wanted = new HashSet<string>(nameList.Split(',', StringSplitOptions.RemoveEmptyEntries).Select(n => n.Trim().ToLowerInvariant() + "_dlg"));
        using var writer = new StreamWriter(outPath);
        int packages = 0, found = 0;
        foreach (string file in Directory.GetFiles(cooked, "*.pcc").OrderBy(f => f))
        {
            string name = Path.GetFileNameWithoutExtension(file);
            if (name.Contains("_LOC_", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }
            try
            {
                using IMEPackage pcc = MEPackageHandler.OpenMEPackage(file, forceLoadFromDisk: true);
                var targets = new Dictionary<int, string>();
                foreach (ImportEntry import in pcc.Imports.Where(i => wanted.Contains(i.ObjectName.Instanced.ToLowerInvariant())))
                {
                    targets[import.UIndex] = import.ObjectName.Instanced;
                }
                foreach (ExportEntry export in pcc.Exports.Where(e => wanted.Contains(e.ObjectName.Instanced.ToLowerInvariant())))
                {
                    targets[export.UIndex] = export.ObjectName.Instanced;
                }
                if (targets.Count == 0)
                {
                    continue;
                }
                ++packages;
                foreach (ExportEntry export in pcc.Exports)
                {
                    PropertyCollection props;
                    try
                    {
                        props = export.GetProperties();
                    }
                    catch
                    {
                        continue;
                    }
                    foreach ((string property, int target) in Pointers(props, ""))
                    {
                        if (targets.TryGetValue(target, out string conversation))
                        {
                            writer.WriteLine($"{conversation}\t{name}\t{export.ClassName}\t{export.InstancedFullPath}\t{property}");
                            ++found;
                        }
                    }
                }
            }
            catch (Exception e)
            {
                Console.Error.WriteLine($"{name}: {e.GetType().Name}: {e.Message}");
            }
        }
        Console.WriteLine($"{packages} packages hold one of the {wanted.Count} conversations; {found} references -> {outPath}");
        return 0;
    }

    private static IEnumerable<(string, int)> Pointers(IEnumerable<Property> props, string prefix)
    {
        foreach (Property p in props)
        {
            string name = prefix + p.Name.Instanced;
            switch (p)
            {
                case ObjectProperty o when o.Value != 0:
                    yield return (name, o.Value);
                    break;
                case StructProperty s:
                    foreach (var inner in Pointers(s.Properties, name + "."))
                    {
                        yield return inner;
                    }
                    break;
                case ArrayProperty<ObjectProperty> objects:
                    foreach (ObjectProperty o in objects.Where(o => o.Value != 0))
                    {
                        yield return (name + "[]", o.Value);
                    }
                    break;
                case ArrayProperty<StructProperty> structs:
                    foreach (StructProperty s in structs)
                    {
                        foreach (var inner in Pointers(s.Properties, name + "[]."))
                        {
                            yield return inner;
                        }
                    }
                    break;
            }
        }
    }
}
