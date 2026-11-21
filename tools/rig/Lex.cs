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

    public static MEGame Parse(string game) => game?.ToUpperInvariant() switch { "LE1" => MEGame.LE1, "LE2" => MEGame.LE2, _ => MEGame.LE3 };

    // `game` is the one the verb is about; `also` other games whose packages it reads or writes (`bodies`: ME3's reference body).
    // Oodle (the games' package compression, RAD's library: never shipped with this tool) is loaded from the game's own folder
    // first (`gameRoot`, the Mass Effect Legendary Edition folder), so the players' build needs nothing of Legendary Explorer's
    // native files; Legendary Explorer's own lookup (the registry, its folder) is the fallback.
    public static void Init(string game, string gameRoot, params string[] also)
    {
        Game = Parse(game);
        if (gameRoot != null)
        {
            foreach (string folder in new[] { "ME1", "ME2", "ME3" })
            {
                string oodle = System.IO.Path.Combine(gameRoot, "Game", folder, "Binaries", "Win64", "oo2core_8_win64.dll");
                if (System.IO.File.Exists(oodle))
                {
                    LegendaryExplorerCore.Compression.OodleHelper.LoadOodleDll(oodle);
                    break;
                }
            }
        }
        var games = new[] { Game }.Concat(also.Select(Parse)).Distinct().ToArray();
        LegendaryExplorerCore.LegendaryExplorerCoreLib.InitLib(TaskScheduler.Default, _ => { }, null, games);
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
            // A whole path names one ("CTHd.X" against "Review.CTHd.X").
            var exact = found.Where(e => e.InstancedFullPath.Equals(pathEnd, StringComparison.OrdinalIgnoreCase)).ToList();
            if (exact.Count == 1)
            {
                return exact[0];
            }
            throw new InvalidOperationException($"{found.Count} SkeletalMeshes end '{pathEnd}': " + string.Join(", ", found.Select(f => f.InstancedFullPath)));
        }
        return found[0];
    }
}
