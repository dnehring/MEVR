// SPDX-License-Identifier: GPL-3.0-only
// The clubs' dancers' bodies (tools/rig/README.md "Club bodies", docs/trilogy-hair-plan.md section 12): the game skins a bone
// from a rotation, a translation and one uniform scale, so the shape the owner chose on the before/after sheets (Sizes.cs: the
// breasts grown mostly outward and a little down, the glutes outward) can't be made by the game at run time. It is baked into
// a copy of each body the clubs' women wear instead, beside the rig in the same package:
//
//   mevr-rig clubs <bodies.pcc> --names <rig>,<rig>... [--middle 0.7] [--glute 0.12] [--breast-shape k,k,k,k] [--glute-shape ...]
//
// For each rig (e.g. HMF_ARM_NKDb_MDL_MEVR) a copy named <rig minus _MEVR>_CLUB_MEVR: the same bones, weights, materials and
// sockets, every LOD's vertices grown by Sizes' growth (breasts a = middle (breastMax - 1), the index's breastMax; glutes a =
// glute), the shading turned with the surface (Fit.TurnNormals), its body spheres for the hair (colliders\<club>.json). The
// package's index (<bodies>.json, mevr-rig-index v1) gets "club" (the copy's object name) and "clubBreastA" (the breasts' baked
// amount) on the rig's entries. The game (game/Hair) wears the copy on a club NPC and adds only a small uniform variation per
// dancer on top. Run after `mevr-rig sizes --write` (breastMax); a copy already in the package is replaced.
// Uses Legendary Explorer's library (GPL-3.0). Reads and writes the player's game data, kept local.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.Packages.CloningImportingAndRelinking;
using LegendaryExplorerCore.Unreal;
using LegendaryExplorerCore.Unreal.BinaryConverters;

namespace Mevr.Rig;

internal static class Clubs
{
    public const float Middle = 0.7f;   // the copy's breasts: this share of the way to breastMax (a middle dancer)
    public const float Glute = 0.12f;   // the copy's glutes

    public static string ClubName(string rig) =>
        rig.EndsWith("_MEVR", StringComparison.OrdinalIgnoreCase) ? rig[..^5] + "_CLUB_MEVR" : rig + "_CLUB";

    public static int Run(string[] args)
    {
        if (args.Length < 2 || Program.Arg(args, "--names") == null)
        {
            Console.Error.WriteLine("mevr-rig clubs <bodies.pcc> --names <rig>,<rig>... [--middle 0.7] [--glute 0.12] [--breast-shape k,k,k,k] [--glute-shape k,k,k,k]");
            return 2;
        }
        var names = Program.Arg(args, "--names").Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        float middle = float.Parse(Program.Arg(args, "--middle") ?? Middle.ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture);
        float glute = float.Parse(Program.Arg(args, "--glute") ?? Glute.ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture);
        var breastShape = Sizes.Shape.Parse(Program.Arg(args, "--breast-shape"), Sizes.Shape.Breasts);
        var gluteShape = Sizes.Shape.Parse(Program.Arg(args, "--glute-shape"), Sizes.Shape.Glutes);
        var (_, failed) = Make(args[1], names, middle, glute, breastShape, gluteShape);
        return failed > 0 ? 1 : 0;
    }

    // The club copies of `names` in the package at `path` and their index fields; returns (made, not found).
    public static (int made, int failed) Make(string path, IReadOnlyList<string> names, float middle, float glute,
        Sizes.Shape breastShape, Sizes.Shape gluteShape)
    {
        path = Path.GetFullPath(path);
        string indexPath = Path.ChangeExtension(path, ".json");
        var root = JsonNode.Parse(File.ReadAllText(indexPath));
        var breastMax = new Dictionary<string, float>(StringComparer.OrdinalIgnoreCase);
        foreach (var e in root["meshes"].AsArray())
        {
            if (e["breastMax"] != null) breastMax[e["name"].GetValue<string>()] = e["breastMax"].GetValue<float>();
        }

        var made = new Dictionary<string, (string club, float a)>(StringComparer.OrdinalIgnoreCase);
        int failed = 0;
        using (var package = MEPackageHandler.OpenMEPackage(path, forceLoadFromDisk: true))
        {
            foreach (string name in names)
            {
                var export = package.Exports.FirstOrDefault(e => e.ClassName == "SkeletalMesh" && e.Parent == null
                    && e.ObjectName.Instanced.Equals(name, StringComparison.OrdinalIgnoreCase));
                if (export == null)
                {
                    Console.WriteLine($"  {name}: not in {Path.GetFileName(path)}");
                    ++failed;
                    continue;
                }
                string club = ClubName(export.ObjectName.Instanced);
                foreach (var old in package.Exports.Where(e => e.Parent == null && e.ObjectName.Instanced.Equals(club, StringComparison.OrdinalIgnoreCase)).ToList())
                {
                    EntryPruner.TrashEntryAndDescendants(old);
                }
                var clone = (ExportEntry)EntryCloner.CloneTree(export);
                clone.ObjectName = NameReference.FromInstancedString(club);
                var mesh = ObjectBinary.From<SkeletalMesh>(clone);
                float a = breastMax.TryGetValue(name, out float max) ? middle * (max - 1f) : 0f;
                int moved = Bake(mesh, a, glute, breastShape, gluteShape);
                clone.WriteBinary(mesh);
                var report = new Report();
                var spheres = Colliders.Fit(mesh, report, 0f);
                Colliders.Write(Colliders.FileFor(path, club), club, spheres);
                made[name] = (club, a);
                Console.WriteLine(string.Create(CultureInfo.InvariantCulture,
                    $"  {club}: breasts a {a:F3} (breastMax {(breastMax.TryGetValue(name, out float m) ? m : 0f):F3}), glutes a {glute:F2}; {moved} vertices moved (every LOD), {spheres.Count} spheres"));
            }
            package.Save();
        }
        int set = 0;
        foreach (var entry in root["meshes"].AsArray())
        {
            if (entry is not JsonObject o) continue;
            o.Remove("club");
            o.Remove("clubBreastA");
            if (made.TryGetValue(o["name"].GetValue<string>(), out var c))
            {
                o["club"] = c.club;
                o["clubBreastA"] = MathF.Round(c.a * 1000f) / 1000f;
                ++set;
            }
        }
        Sizes.WriteIndex(indexPath, root);
        Console.WriteLine($"{Path.GetFileName(path)}: {made.Count} club bodies, {failed} not found; index {indexPath}: club on {set} entries, {new FileInfo(path).Length / 1024} KB");
        return (made.Count, failed);
    }

    // Every LOD's vertices grown in place (Sizes' growth through each LOD's own weights); returns how many moved.
    public static int Bake(SkeletalMesh mesh, float breastA, float gluteA, Sizes.Shape breastShape, Sizes.Shape gluteShape)
    {
        (float, Sizes.Shape, float) GrowthOf(string bone) =>
            bone.StartsWith("Breast", StringComparison.OrdinalIgnoreCase) ? (breastA, breastShape, 1f) :
            bone.StartsWith("Glute", StringComparison.OrdinalIgnoreCase) ? (gluteA, gluteShape, -1f) : (0f, breastShape, 1f);
        int moved = 0;
        for (int l = 0; l < mesh.LODModels.Length; ++l)
        {
            var lod = mesh.LODModels[l];
            var body = Sizes.Load(mesh, l);
            var data = lod.VertexBufferGPUSkin.VertexData;
            var before = data.Select(g => g.Position).ToArray();
            for (int v = 0; v < data.Length; ++v)
            {
                var p = Sizes.Grown(body, v, GrowthOf);
                if (p == data[v].Position) continue;
                data[v].Position = p;
                ++moved;
            }
            Fit.TurnNormals(lod.IndexBuffer, before, data);
        }
        return moved;
    }
}
