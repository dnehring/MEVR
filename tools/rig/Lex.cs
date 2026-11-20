// SPDX-License-Identifier: GPL-3.0-only
// The Legendary Explorer side of tools/rig: opening a package and finding a skeletal mesh in it.
using System;
using System.Linq;
using System.Threading.Tasks;
using LegendaryExplorerCore.Packages;

namespace Mevr.Rig;

internal static class Lex
{
    public static MEGame Game { get; private set; } = MEGame.LE3;

    public static void Init(string game)
    {
        Game = game switch { "LE1" => MEGame.LE1, "LE2" => MEGame.LE2, _ => MEGame.LE3 };
        LegendaryExplorerCore.LegendaryExplorerCoreLib.InitLib(TaskScheduler.Default, _ => { }, null, new[] { Game });
    }

    public static ExportEntry FindMesh(IMEPackage package, string pathEnd)
    {
        var found = package.Exports.Where(e => e.ClassName == "SkeletalMesh" &&
            e.InstancedFullPath.EndsWith(pathEnd, StringComparison.OrdinalIgnoreCase)).ToList();
        if (found.Count == 0)
        {
            throw new InvalidOperationException($"no SkeletalMesh ending '{pathEnd}' in {package.FilePath}");
        }
        if (found.Count > 1)
        {
            throw new InvalidOperationException($"{found.Count} SkeletalMeshes end '{pathEnd}': " + string.Join(", ", found.Select(f => f.InstancedFullPath)));
        }
        return found[0];
    }
}
