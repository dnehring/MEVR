// SPDX-License-Identifier: GPL-3.0-only
// Automatic placement of the soft-tissue bones (tools/rig/README.md "Automatic placement", docs/trilogy-hair-plan.md section
// 12): the breast and glute bones and their weight regions read from a body's own surface, as was done by hand for Miranda
// and Liara (plans/le3-miranda.jsonc, plans/le3-liara.jsonc). `"place": "auto"` in a plan's step, or `mevr-rig place`.
//
// The surface is a height field: LOD 0's triangles seen from the front (the furthest x per cell of y and z, 0.5 cm cells) for
// the chest, from the back (the nearest) for the hips, averaged with its mirror image (a suit's zip or a seam on one side
// moved Miranda's left apex 3 cm to the middle). Per mound, from that field:
// - the apex: the furthest cell in the band (the chest: Chest2's height -15..+5.5 cm, 2..12 cm off the middle; the hips:
//   Pelvis's -17..+5), its depth the furthest point (x), its height (z) from the field smoothed over `ApexSigma`;
// - its side (y): the middle of the run of cells across the apex's height that stand within `SpanDepth` of the apex, its
//   inner end no nearer the middle than `InnerY` (breasts that touch would put it at the cleavage);
// - the bone `BoneIn` behind the apex (inside the body), on Chest2 (breasts) or Pelvis (glutes), its axes the mesh's turned
//   (0 or 180 degrees: X out of the body); the region's centre `RegionIn` behind the apex, its half axes the hand plans'.
// Every distance scales with the skeleton: Chest2's height over Miranda's (140.4) for the chest, Pelvis's (101.2) for the hips.
// Uses Legendary Explorer's library (GPL-3.0). Reads the player's game data; writes nothing of it.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using LegendaryExplorerCore.Unreal.BinaryConverters;

namespace Mevr.Rig;

internal static class Placement
{
    const float Cell = 0.5f;
    const float RefChest2Z = 140.4f;  // Miranda's (ME3 HMF_ARM_MIRa) rest heights: the hand placements' skeleton
    const float RefPelvisZ = 101.2f;
    const float MinCover = 0.8f;      // the share of the band's height the surface must cover at the apex's side
    const float EdgeCm = 1.0f;        // an apex this near the band's edge is a rim, not a mound

    internal sealed class Mound
    {
        public string Name;          // "Breast" or "Glute"
        public string Parent;        // Chest2 or Pelvis
        public bool Front;
        public float ZLo, ZHi;       // the band round the parent's height (cm, at Miranda's size)
        public float BoneIn, RegionIn, ApexSigma, SpanDepth, InnerY, ZOffsetBone, ZOffsetRegion;
        public float[] Radius;
        public float Weight;
        public string[] From;
        public float Yaw;
    }

    // The rule's numbers, fitted to the three hand placements (research/trilogy-hair.md "Automatic placement").
    public static readonly Mound Breasts = new()
    {
        Name = "Breast", Parent = "Chest2", Front = true, ZLo = -15f, ZHi = 5.5f,
        BoneIn = 7.5f, RegionIn = 3.5f, ApexSigma = 0.75f, SpanDepth = 1.5f, InnerY = 4f, ZOffsetBone = -0.75f, ZOffsetRegion = -0.25f,
        Radius = new[] { 6.5f, 7.0f, 8.0f }, Weight = 0.9f, From = new[] { "Chest", "Chest1", "Chest2" }, Yaw = 0f,
    };
    public static readonly Mound Glutes = new()
    {
        Name = "Glute", Parent = "Pelvis", Front = false, ZLo = -17f, ZHi = 5f,
        BoneIn = 7.0f, RegionIn = 4.25f, ApexSigma = 1.5f, SpanDepth = 1.5f, InnerY = 3.5f, ZOffsetBone = -0.25f, ZOffsetRegion = -0.5f,
        Radius = new[] { 7.0f, 7.5f, 10.0f }, Weight = 0.85f, From = new[] { "Pelvis", "LowerBack", "Root" }, Yaw = 180f,
    };

    public sealed record Result(List<BonePlan> Bones, List<RegionPlan> Regions, List<string> Notes);

    public static Result Auto(SkeletalMesh mesh)
    {
        var bones = new List<BonePlan>();
        var regions = new List<RegionPlan>();
        var notes = new List<string>();
        var rest = Skeleton.Rest(mesh);
        var lod = mesh.LODModels[0];
        var data = lod.VertexBufferGPUSkin.VertexData;
        var pos = data.Select(g => new Vector3(g.Position.X, g.Position.Y, g.Position.Z)).ToArray();
        foreach (var m in new[] { Breasts, Glutes })
        {
            int parent = Bone(mesh, m.Parent);
            if (parent < 0)
            {
                notes.Add($"{m.Name.ToLowerInvariant()}: none (no {m.Parent} bone: not the human female skeleton)");
                continue;
            }
            float pz = rest[parent].Position.Z;
            float s = pz / (m.Front ? RefChest2Z : RefPelvisZ);
            float zlo = pz + m.ZLo * s, zhi = pz + m.ZHi * s;
            var field = Field(pos, lod.IndexBuffer, zlo - 3f * s, zhi + 3f * s, 30f * s, m.Front);
            var sym = Mirror(field);
            // The apex: the furthest cell 2..12 cm off the middle inside the band that stands out along its column (at least as
            // far out as the cells 1 cm above and below it): an armour's belly plate or belt, further out at the band's edge than
            // the chest, isn't it (ME1's medium and light armour).
            (int y, int z) apex = default;
            float best = float.NegativeInfinity;
            int step = (int)MathF.Round(1f / Cell);
            foreach (var (k, v) in sym)
            {
                float y = k.y * Cell, z = k.z * Cell;
                if (y < 2f * s || y > 12f * s || z < zlo + EdgeCm * s || z > zhi - EdgeCm * s || v <= best) continue;
                if (!sym.TryGetValue((k.y, k.z - step), out float below) || !sym.TryGetValue((k.y, k.z + step), out float above)) continue;
                if (v < below || v < above) continue;
                best = v;
                apex = k;
            }
            if (float.IsNegativeInfinity(best))
            {
                notes.Add($"{m.Name.ToLowerInvariant()}: none (no surface {(m.Front ? "in front" : "behind")} standing out in z {zlo:F1}..{zhi:F1}: a part of an outfit without it)");
                continue;
            }
            // A mound only where the mesh covers the whole band (ME3's armour comes in parts: a torso's lower rim isn't a glute).
            int cells = 0, covered = 0;
            for (int iz = (int)MathF.Ceiling(zlo / Cell); iz <= (int)MathF.Floor(zhi / Cell); ++iz, ++cells)
            {
                if (sym.ContainsKey((apex.y, iz))) ++covered;
            }
            float cover = cells > 0 ? (float)covered / cells : 0f;
            if (cover < MinCover)
            {
                notes.Add($"{m.Name.ToLowerInvariant()}: none (the surface covers {cover:P0} of z {zlo:F1}..{zhi:F1} at y {apex.y * Cell:F1}: a part of an outfit without it)");
                continue;
            }
            // Its height from the smoothed field's furthest cell near the apex (within 3 cm up or down, 2 cm sideways: a single
            // vertex's spike moves the raw one).
            var smooth = Smooth(sym, m.ApexSigma);
            (int y, int z) high = apex;
            float bestSmooth = float.NegativeInfinity;
            int near = (int)MathF.Round(3f * s / Cell), aside = (int)MathF.Round(2f * s / Cell);
            foreach (var (k, v) in smooth)
            {
                float y = k.y * Cell;
                if (y < 2f * s || y > 12f * s || Math.Abs(k.z - apex.z) > near || Math.Abs(k.y - apex.y) > aside || v <= bestSmooth) continue;
                bestSmooth = v;
                high = k;
            }
            // Its side: the middle of the run across the apex's height within SpanDepth of it.
            float limit = best - m.SpanDepth * s;
            int lo = apex.y, hi = apex.y;
            while (sym.TryGetValue((lo - 1, apex.z), out float a) && a > limit) --lo;
            while (sym.TryGetValue((hi + 1, apex.z), out float b) && b > limit) ++hi;
            float inner = MathF.Max(lo * Cell, m.InnerY * s), outer = hi * Cell;
            float side = (inner + outer) / 2f;
            float depth = m.Front ? best : -best;     // the apex's x
            float inward = m.Front ? -1f : 1f;        // toward the body's core
            float zApex = high.z * Cell;
            notes.Add($"{m.Name.ToLowerInvariant()}: apex x {depth:F1} at y +-{apex.y * Cell:F1} z {apex.z * Cell:F1} (smoothed z {zApex:F1}), the run y {lo * Cell:F1}..{outer:F1} (inner end {inner:F1}), scale {s:F3}");
            foreach (var (suffix, sign) in new[] { ("Left", -1f), ("Right", 1f) })
            {
                string name = m.Name + suffix;
                bones.Add(new BonePlan
                {
                    Name = name, Parent = m.Parent, Yaw = m.Yaw,
                    At = new[] { R(depth + inward * m.BoneIn * s), R(sign * side), R(zApex + m.ZOffsetBone * s) },
                });
                regions.Add(new RegionPlan
                {
                    Bone = name, Weight = m.Weight, Inner = 0.3f,
                    Center = new[] { R(depth + inward * m.RegionIn * s), R(sign * side), R(zApex + m.ZOffsetRegion * s) },
                    Radius = m.Radius.Select(r => R(r * s)).ToArray(),
                    From = m.From.Where(f => Bone(mesh, f) >= 0).ToArray(),
                });
            }
        }
        return new Result(bones, regions, notes);
    }

    static float R(float v) => MathF.Round(v * 10f) / 10f;

    static int Bone(SkeletalMesh mesh, string name) =>
        Array.FindIndex(mesh.RefSkeleton, b => b.Name.Instanced.Equals(name, StringComparison.OrdinalIgnoreCase));

    // The furthest (front) or nearest (back) x of the triangles per cell of (y, z), as +x (front) or -x (back).
    static Dictionary<(int y, int z), float> Field(Vector3[] p, ushort[] index, float zlo, float zhi, float yMax, bool front)
    {
        float sign = front ? 1f : -1f;
        var h = new Dictionary<(int, int), float>();
        for (int t = 0; t + 2 < index.Length; t += 3)
        {
            Vector3 a = p[index[t]], b = p[index[t + 1]], c = p[index[t + 2]];
            if (MathF.Max(a.Z, MathF.Max(b.Z, c.Z)) < zlo || MathF.Min(a.Z, MathF.Min(b.Z, c.Z)) > zhi) continue;
            if (MathF.Max(MathF.Abs(a.Y), MathF.Max(MathF.Abs(b.Y), MathF.Abs(c.Y))) > yMax) continue;
            float d = (b.Y - a.Y) * (c.Z - a.Z) - (c.Y - a.Y) * (b.Z - a.Z);
            if (MathF.Abs(d) < 1e-9f) continue;
            int y0 = (int)MathF.Floor(MathF.Min(a.Y, MathF.Min(b.Y, c.Y)) / Cell), y1 = (int)MathF.Ceiling(MathF.Max(a.Y, MathF.Max(b.Y, c.Y)) / Cell);
            int z0 = (int)MathF.Floor(MathF.Min(a.Z, MathF.Min(b.Z, c.Z)) / Cell), z1 = (int)MathF.Ceiling(MathF.Max(a.Z, MathF.Max(b.Z, c.Z)) / Cell);
            for (int iy = y0; iy <= y1; ++iy)
            {
                for (int iz = z0; iz <= z1; ++iz)
                {
                    float y = iy * Cell, z = iz * Cell;
                    if (z < zlo || z > zhi) continue;
                    float u = ((b.Y - y) * (c.Z - z) - (c.Y - y) * (b.Z - z)) / d;
                    float w = ((c.Y - y) * (a.Z - z) - (a.Y - y) * (c.Z - z)) / d;
                    float v = 1f - u - w;
                    if (u < -1e-6f || w < -1e-6f || v < -1e-6f) continue;
                    float x = sign * (u * a.X + w * b.X + v * c.X);
                    if (!h.TryGetValue((iy, iz), out float old) || x > old) h[(iy, iz)] = x;
                }
            }
        }
        return h;
    }

    static Dictionary<(int y, int z), float> Mirror(Dictionary<(int y, int z), float> h)
    {
        var o = new Dictionary<(int, int), float>(h.Count);
        foreach (var (k, v) in h)
        {
            o[k] = h.TryGetValue((-k.y, k.z), out float m) ? (v + m) / 2f : v;
        }
        return o;
    }

    static Dictionary<(int y, int z), float> Smooth(Dictionary<(int y, int z), float> h, float sigma)
    {
        if (sigma <= 0f) return h;
        int r = (int)MathF.Ceiling(2f * sigma / Cell);
        var o = new Dictionary<(int, int), float>(h.Count);
        foreach (var k in h.Keys)
        {
            double num = 0, den = 0;
            for (int dy = -r; dy <= r; ++dy)
            {
                for (int dz = -r; dz <= r; ++dz)
                {
                    if (!h.TryGetValue((k.y + dy, k.z + dz), out float q)) continue;
                    double w = Math.Exp(-((dy * Cell) * (dy * Cell) + (dz * Cell) * (dz * Cell)) / (2 * sigma * sigma));
                    num += w * q;
                    den += w;
                }
            }
            o[k] = (float)(num / den);
        }
        return o;
    }

    // mevr-rig place <package> <mesh> [--plan plan.jsonc --step id]: the automatic placement, and with a plan its distance from
    // that step's hand-placed bones and regions (cm).
    public static int Run(string[] args)
    {
        using var package = LegendaryExplorerCore.Packages.MEPackageHandler.OpenMEPackage(args[1], forceLoadFromDisk: true);
        var mesh = ObjectBinary.From<SkeletalMesh>(Lex.FindMesh(package, args[2]));
        var r = Auto(mesh);
        foreach (string n in r.Notes) Console.WriteLine("  " + n);
        foreach (var b in r.Bones) Console.WriteLine($"  bone {b.Name} on {b.Parent} at ({string.Join(", ", b.At)}) yaw {b.Yaw}");
        foreach (var g in r.Regions) Console.WriteLine($"  region {g.Bone} centre ({string.Join(", ", g.Center)}) radius ({string.Join(", ", g.Radius)})");
        string planPath = Program.Arg(args, "--plan");
        if (planPath == null) return 0;
        var plan = Rigging.ReadPlan(planPath);
        var step = plan.Steps.First(s => string.Equals(s.Id, Program.Arg(args, "--step"), StringComparison.OrdinalIgnoreCase));
        float worst = 0, worstAxis = 0;
        foreach (var hand in step.Bones)
        {
            var auto = r.Bones.First(b => b.Name == hand.Name);
            float d = Vector3.Distance(V(hand.At), V(auto.At));
            worst = MathF.Max(worst, d);
            worstAxis = MathF.Max(worstAxis, AxisMax(hand.At, auto.At));
            Console.WriteLine($"  delta bone {hand.Name}: {d:F2} cm, axes {Axes(hand.At, auto.At)} (hand {string.Join(", ", hand.At)})");
        }
        foreach (var hand in step.Regions)
        {
            var auto = r.Regions.First(g => g.Bone == hand.Bone);
            float d = Vector3.Distance(V(hand.Center), V(auto.Center));
            worst = MathF.Max(worst, d);
            worstAxis = MathF.Max(worstAxis, AxisMax(hand.Center, auto.Center));
            Console.WriteLine($"  delta region {hand.Bone}: {d:F2} cm, axes {Axes(hand.Center, auto.Center)} (hand {string.Join(", ", hand.Center)})");
        }
        // Within 1 cm on each axis (the hand placements are read to 0.5 cm, and disagree by up to 2 cm in height between two
        // bodies whose surfaces are the same within 3 mm: research/trilogy-hair.md "Automatic placement").
        bool ok = worstAxis <= 1.05f;
        Console.WriteLine($"  worst {worst:F2} cm apart, {worstAxis:F2} cm on one axis{(ok ? "" : ": over 1 cm")}");
        return ok ? 0 : 1;
    }

    static Vector3 V(float[] a) => new(a[0], a[1], a[2]);
    static float AxisMax(float[] a, float[] b) => MathF.Max(MathF.Abs(b[0] - a[0]), MathF.Max(MathF.Abs(b[1] - a[1]), MathF.Abs(b[2] - a[2])));
    static string Axes(float[] a, float[] b) => $"x {b[0] - a[0]:+0.0;-0.0} y {MathF.Abs(b[1]) - MathF.Abs(a[1]):+0.0;-0.0} z {b[2] - a[2]:+0.0;-0.0}";
}
