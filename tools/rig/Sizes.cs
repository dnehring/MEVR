// SPDX-License-Identifier: GPL-3.0-only
// The soft tissue's size limits and a preview of them (tools/rig/README.md "Sizes", docs/trilogy-hair-plan.md section 12): the
// game (game/Hair, the clubs' dancers) grows each Breast*/Glute* bone by an amount a, mostly outward from the body and a little
// down (the owner, 2026-11-20: grown the same every way they looked rounder and higher). In the mesh's bind frame (x forward,
// y side, z up; for a glute "out" is -x, the glute bone's own X since its yaw is 180) about the bone's bind place P, a vertex's
// share on the bone moves to P + G (v - P), G linear:
//   out' = out (1 + a kOut), y' = y (1 + a kSide), z' = z (1 + a kUp) - a kDrop out'
// (the last a sag: what stands further out drops more). Shape.Breasts and Shape.Glutes hold the k's; the game's runtime uses
// the same G conjugated into the bone's frame. This file says how far each rigged body may go and shows it.
//
// - `mevr-rig sizes <bodies.pcc> --ref <package> <mesh> [--write]`: per rigged body, `breastMax` = 1 + the largest a at which
//   its breast peak (the furthest forward point of the vertices weighted to that side's breast bone, grown, from Chest2's rest
//   x) reaches the reference's grown by `RefAmount` (the owner: up to 10% larger than ME3 Liara's romance body, ASA_ARM_LIAd),
//   the smaller of the two sides, clamped 1.0..1.8 (for LIAd itself 1.10). The real weights count: a vertex weighted 0.6 moves
//   0.6 of the bone's growth. With --write it goes into the package's index (<bodies>.json, mevr-rig-index v1, an added field).
// - `mevr-rig export <package> <mesh> <out.obj> [--breast 1+a] [--glute 1+a] [--breast-shape kOut,kSide,kUp,kDrop]
//   [--glute-shape ...]`: LOD 0 in its bind pose as OBJ (cm, z up), the growth applied as above; for the previews
//   (tools/rig/preview-sizes.ps1). The output is the player's game data: local only.
// Uses Legendary Explorer's library (GPL-3.0).
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.Unreal.BinaryConverters;

namespace Mevr.Rig;

internal static class Sizes
{
    public const float MaxScale = 1.8f;
    public const float RefAmount = 0.10f;   // the cap: the reference's breasts grown by this much

    // The growth's shape (kOut, kSide, kUp, kDrop): see the file's head.
    public readonly record struct Shape(float Out, float Side, float Up, float Drop)
    {
        public static readonly Shape Breasts = new(1.0f, 0.45f, 0.15f, 0.18f);  // tuned on the renders, 2026-11-20
        public static readonly Shape Glutes = new(1.0f, 0.35f, 0.3f, 0.15f);

        public static Shape Parse(string text, Shape fallback)
        {
            if (string.IsNullOrEmpty(text)) return fallback;
            var f = text.Split(',').Select(t => float.Parse(t, CultureInfo.InvariantCulture)).ToArray();
            return new Shape(f[0], f[1], f[2], f[3]);
        }

        // v - P grown by a; outSign: +1 for a breast (x forward), -1 for a glute.
        public Vector3 Grow(Vector3 rel, float a, float outSign)
        {
            float o = outSign * rel.X * (1f + a * Out);
            float y = rel.Y * (1f + a * Side);
            float z = rel.Z * (1f + a * Up) - a * Drop * o;
            return new Vector3(outSign * o, y, z);
        }
    }

    // One vertex's rest position and its weight on each soft-tissue bone (skeleton index -> 0..1).
    sealed class Body
    {
        public SkeletalMesh Mesh;
        public Vector3[] Pos;
        public Dictionary<int, float>[] Soft;   // per vertex: soft-tissue bone -> weight (null: none)
        public List<Skeleton.Pose> Rest;
        public uint[] Index;
    }

    static Body Load(SkeletalMesh mesh)
    {
        var lod = mesh.LODModels[0];
        var data = lod.VertexBufferGPUSkin.VertexData;
        var b = new Body
        {
            Mesh = mesh,
            Rest = Skeleton.Rest(mesh),
            Pos = data.Select(g => new Vector3(g.Position.X, g.Position.Y, g.Position.Z)).ToArray(),
            Soft = new Dictionary<int, float>[data.Length],
            Index = lod.IndexBuffer.Select(i => (uint)i).ToArray(),
        };
        var soft = new bool[mesh.RefSkeleton.Length];
        for (int i = 0; i < soft.Length; ++i)
        {
            string n = mesh.RefSkeleton[i].Name.Instanced;
            soft[i] = n.StartsWith("Breast", StringComparison.OrdinalIgnoreCase) || n.StartsWith("Glute", StringComparison.OrdinalIgnoreCase);
        }
        foreach (var chunk in lod.Chunks)
        {
            int count = chunk.NumRigidVertices + chunk.NumSoftVertices;
            for (int i = 0; i < count; ++i)
            {
                int v = (int)chunk.BaseVertexIndex + i;
                var g = data[v];
                for (int k = 0; k < 4; ++k)
                {
                    int local = g.InfluenceBones[k];
                    float w = g.InfluenceWeights[k] / 255f;
                    if (w <= 0f || local >= chunk.BoneMap.Length) continue;
                    int bone = chunk.BoneMap[local];
                    if (!soft[bone]) continue;
                    b.Soft[v] ??= new Dictionary<int, float>();
                    b.Soft[v][bone] = b.Soft[v].GetValueOrDefault(bone) + w;
                }
            }
        }
        return b;
    }

    static int Bone(SkeletalMesh mesh, string name) =>
        Array.FindIndex(mesh.RefSkeleton, b => b.Name.Instanced.Equals(name, StringComparison.OrdinalIgnoreCase));

    // The vertex grown: v + sum over soft bones of w (G (v - P) - (v - P)).
    static Vector3 Grown(Body b, int v, Func<string, (float a, Shape shape, float outSign)> growthOf)
    {
        var p = b.Pos[v];
        if (b.Soft[v] == null) return p;
        var d = Vector3.Zero;
        foreach (var (bone, w) in b.Soft[v])
        {
            var (a, shape, outSign) = growthOf(b.Mesh.RefSkeleton[bone].Name.Instanced);
            if (a == 0f) continue;
            var rel = p - b.Rest[bone].Position;
            d += w * (shape.Grow(rel, a, outSign) - rel);
        }
        return p + d;
    }

    // The breast peak of one side grown by a, from Chest2's rest x: the furthest forward point of the vertices weighted to it
    // (only the outward growth moves x).
    static float Peak(Body b, int bone, int chest2, float a)
    {
        float best = float.NegativeInfinity;
        var p0 = b.Rest[bone].Position;
        for (int v = 0; v < b.Pos.Length; ++v)
        {
            if (b.Soft[v] == null || !b.Soft[v].TryGetValue(bone, out float w)) continue;
            float x = b.Pos[v].X + w * a * Shape.Breasts.Out * (b.Pos[v].X - p0.X);
            best = MathF.Max(best, x);
        }
        return best - b.Rest[chest2].Position.X;
    }

    public sealed record Limit(float BreastMax, float PeakLeft, float PeakRight, float RefLeft, float RefRight);

    // The largest s in 1..MaxScale at which each side's peak reaches the reference's, the smaller side; null: no breast bones.
    static Limit BreastLimit(Body b, Body reference)
    {
        int chest2 = Bone(b.Mesh, "Chest2"), refChest2 = Bone(reference.Mesh, "Chest2");
        if (chest2 < 0 || refChest2 < 0) return null;
        float limit = MaxScale;
        var peaks = new List<float>();
        var refs = new List<float>();
        foreach (string side in new[] { "BreastLeft", "BreastRight" })
        {
            int bone = Bone(b.Mesh, side), refBone = Bone(reference.Mesh, side);
            if (bone < 0 || refBone < 0) return null;
            float target = Peak(reference, refBone, refChest2, RefAmount);
            float now = Peak(b, bone, chest2, 0f);
            peaks.Add(now);
            refs.Add(target);
            if (float.IsNegativeInfinity(now)) return null;
            float aMax = MaxScale - 1f, a;
            if (now >= target) a = 0f;
            else if (Peak(b, bone, chest2, aMax) <= target) a = aMax;
            else
            {
                float lo = 0f, hi = aMax;
                for (int i = 0; i < 40; ++i)
                {
                    float mid = (lo + hi) / 2f;
                    if (Peak(b, bone, chest2, mid) < target) lo = mid; else hi = mid;
                }
                a = lo;
            }
            limit = MathF.Min(limit, 1f + a);
        }
        return new Limit(MathF.Round(limit * 1000f) / 1000f, peaks[0], peaks[1], refs[0], refs[1]);
    }

    static SkeletalMesh MeshOf(IMEPackage package, string name) => ObjectBinary.From<SkeletalMesh>(Lex.FindMesh(package, name));

    public static int Run(string[] args)
    {
        string bodiesPath = args[1];
        int refAt = Array.IndexOf(args, "--ref");
        if (refAt < 0 || refAt + 2 >= args.Length)
        {
            Console.Error.WriteLine("mevr-rig sizes <bodies.pcc> --ref <package> <mesh> [--write]");
            return 2;
        }
        Body reference;
        using (var refPackage = MEPackageHandler.OpenMEPackage(args[refAt + 1], forceLoadFromDisk: true))
        {
            reference = Load(MeshOf(refPackage, args[refAt + 2]));
        }
        bool write = args.Contains("--write");
        using var package = MEPackageHandler.OpenMEPackage(bodiesPath, forceLoadFromDisk: true);
        var limits = new Dictionary<string, Limit>(StringComparer.OrdinalIgnoreCase);
        foreach (var e in package.Exports.Where(e => e.ClassName == "SkeletalMesh"))
        {
            if (limits.ContainsKey(e.ObjectName.Instanced)) continue;
            var body = Load(ObjectBinary.From<SkeletalMesh>(e));
            var l = BreastLimit(body, reference);
            if (l == null) continue;
            limits[e.ObjectName.Instanced] = l;
            Console.WriteLine(string.Create(CultureInfo.InvariantCulture,
                $"  {e.ObjectName.Instanced,-36} breastMax {l.BreastMax:F3}  peak L {l.PeakLeft:F1} R {l.PeakRight:F1} (ref {l.RefLeft:F1}, {l.RefRight:F1}) cm ahead of Chest2"));
        }
        var values = limits.Values.Select(l => l.BreastMax).OrderBy(v => v).ToList();
        if (values.Count > 0)
        {
            Console.WriteLine(string.Create(CultureInfo.InvariantCulture,
                $"{Path.GetFileName(bodiesPath)}: {values.Count} bodies with breasts; breastMax min {values[0]:F3}, median {values[values.Count / 2]:F3}, max {values[^1]:F3}; " +
                $"at 1.0 (as large as the reference or larger) {values.Count(v => v <= 1.0005f)}, at the cap {MaxScale} {values.Count(v => v >= MaxScale - 0.0005f)}"));
        }
        if (!write) return 0;
        string indexPath = Path.ChangeExtension(bodiesPath, ".json");
        var root = JsonNode.Parse(File.ReadAllText(indexPath));
        int set = 0;
        foreach (var entry in root["meshes"].AsArray())
        {
            string name = entry["name"].GetValue<string>();
            if (entry is JsonObject o)
            {
                o.Remove("breastMax");
                if (limits.TryGetValue(name, out var l))
                {
                    o["breastMax"] = l.BreastMax;
                    ++set;
                }
            }
        }
        WriteIndex(indexPath, root);
        Console.WriteLine($"{indexPath}: breastMax on {set} of {root["meshes"].AsArray().Count} entries");
        return 0;
    }

    // The index in WriteIndex's layout (one entry a line), with the added fields.
    static void WriteIndex(string path, JsonNode root)
    {
        var o = new StringBuilder();
        o.Append("{\n  \"format\": ").Append(root["format"].ToJsonString()).Append(",\n  \"version\": ").Append(root["version"].ToJsonString())
         .Append(",\n  \"package\": ").Append(root["package"].ToJsonString()).Append(",\n  \"meshes\": [\n");
        var meshes = root["meshes"].AsArray();
        for (int i = 0; i < meshes.Count; ++i)
        {
            o.Append("    { ");
            o.Append(string.Join(", ", meshes[i].AsObject().Select(kv => JsonSerializer.Serialize(kv.Key) + ": " + kv.Value.ToJsonString())));
            o.Append(i + 1 < meshes.Count ? " },\n" : " }\n");
        }
        o.Append("  ]\n}\n");
        File.WriteAllText(path, o.ToString());
    }

    public static int Export(string[] args)
    {
        if (args.Length < 4)
        {
            Console.Error.WriteLine("mevr-rig export <package> <mesh> <out.obj> [--breast 1+a] [--glute 1+a] [--breast-shape k,k,k,k] [--glute-shape k,k,k,k]");
            return 2;
        }
        float breast = float.Parse(Program.Arg(args, "--breast") ?? "1", CultureInfo.InvariantCulture);
        float glute = float.Parse(Program.Arg(args, "--glute") ?? "1", CultureInfo.InvariantCulture);
        using var package = MEPackageHandler.OpenMEPackage(args[1], forceLoadFromDisk: true);
        var mesh = MeshOf(package, args[2]);
        int fitAt = Array.IndexOf(args, "--fit");
        if (fitAt >= 0 && fitAt + 2 < args.Length)
        {
            // The skirt fit (Fit) on a game mesh as the rig would bake it: --fit <reference package> <reference mesh>.
            using var refPackage = MEPackageHandler.OpenMEPackage(args[fitAt + 1], forceLoadFromDisk: true);
            var profile = Fit.Reference(MeshOf(refPackage, args[fitAt + 2]));
            var verdict = Fit.Detect(mesh, profile);
            Console.Error.WriteLine("fit: " + verdict.Why);
            if (verdict.Skirt || args.Contains("--force-fit"))
            {
                var (fitMoved, fitPeak) = Fit.Apply(mesh, profile, verdict.K);
                Console.Error.WriteLine($"fit: {fitMoved} vertices moved, at most {fitPeak:F1} cm");
            }
        }
        var body = Load(mesh);
        var breastShape = Shape.Parse(Program.Arg(args, "--breast-shape"), Shape.Breasts);
        var gluteShape = Shape.Parse(Program.Arg(args, "--glute-shape"), Shape.Glutes);
        (float, Shape, float) GrowthOf(string bone) =>
            bone.StartsWith("Breast", StringComparison.OrdinalIgnoreCase) ? (breast - 1f, breastShape, 1f) :
            bone.StartsWith("Glute", StringComparison.OrdinalIgnoreCase) ? (glute - 1f, gluteShape, -1f) : (0f, breastShape, 1f);
        var o = new StringBuilder();
        o.Append("# mevr-rig export: ").Append(args[2]).Append(string.Create(CultureInfo.InvariantCulture, $" breast {breast} glute {glute}\n"));
        int moved = 0;
        for (int v = 0; v < body.Pos.Length; ++v)
        {
            var p = Grown(body, v, GrowthOf);
            if (p != body.Pos[v]) ++moved;
            o.Append(string.Create(CultureInfo.InvariantCulture, $"v {p.X:F4} {p.Y:F4} {p.Z:F4}\n"));
        }
        for (int t = 0; t + 2 < body.Index.Length; t += 3)
        {
            o.Append(string.Create(CultureInfo.InvariantCulture, $"f {body.Index[t] + 1} {body.Index[t + 1] + 1} {body.Index[t + 2] + 1}\n"));
        }
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[3])));
        File.WriteAllText(args[3], o.ToString());
        Console.WriteLine($"{args[3]}: {body.Pos.Length} vertices ({moved} moved), {body.Index.Length / 3} triangles");
        return 0;
    }
}
