// SPDX-License-Identifier: GPL-3.0-only
// Hair chains for mevr-rig (tools/rig/README.md "Hair chains"): bone chains generated from a hair mesh's own geometry and
// the hair re-weighted to them, so a hairstyle modelled as one shell bends in several places (game/Hair simulates every
// chain whose bones are named *Hair*).
//
// Chains stand at azimuths around the scalp bone's vertical axis (0 front, 90 the character's right, 180 back). Each runs
// from `rootZ` (the hairline, where the hair leaves the head) down to its sector's lowest vertex, its joints evenly spaced
// in height, each joint at the centroid of its sector's vertices at that height (the middle of the hair's thickness).
// Weights: around the head a vertex is shared by the two chains its azimuth lies between (linear in angle, the face's gap
// clamped to the nearest chain); along a chain by the standard smooth chain blend (each bone's weight a hat centred on its
// segment's middle, so a point between two joints is shared by the bones on either side); above the root the scalp bone
// takes over within `blendAboveCm`. Every vertex of the mesh is re-weighted (the hairstyle's old hair bones are left in the
// skeleton, unused by its skin).
using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using LegendaryExplorerCore.Unreal.BinaryConverters;

namespace Mevr.Rig;

internal sealed class SectorPlan
{
    public string Name { get; set; }
    public float Az { get; set; }
}

internal sealed class HairPlan
{
    public string Scalp { get; set; } = "Head";
    public string Prefix { get; set; } = "HairChain";
    public float RootZ { get; set; } = 168f;
    public float BlendAboveCm { get; set; } = 4f;
    public int Joints { get; set; } = 4;
    public float SectorWidthDeg { get; set; } = 40f;  // the vertices a joint's centroid is taken from: within half this of the chain
    public float BandCm { get; set; } = 2f;           // and within half this of the joint's height
    public List<SectorPlan> Sectors { get; set; } = new();

    // Filled by Plan(): the generated bones, in chain order.
    internal List<BonePlan> Generated { get; } = new();
    internal List<Chain> Chains { get; } = new();

    internal sealed class Chain
    {
        public SectorPlan Sector;
        public List<Vector3> Joints = new();  // joint 0 at the root ... the last joint; then the tip
        public Vector3 Tip;
        public List<string> Bones = new();
    }
}

internal static class HairChains
{
    public static float Azimuth(Vector3 p, Vector3 axis) => MathF.Atan2(p.Y - axis.Y, p.X - axis.X) * 180f / MathF.PI;

    public static float AngleBetween(float a, float b)
    {
        float d = (a - b) % 360f;
        if (d > 180f) d -= 360f;
        if (d < -180f) d += 360f;
        return d;
    }

    // The chains' joints from the LOD 0 vertices; the bones to add (each parented to the previous, the first to the scalp).
    public static void Plan(SkeletalMesh mesh, HairPlan plan, Report report)
    {
        var rest = Skeleton.Rest(mesh);
        int scalp = Array.FindIndex(mesh.RefSkeleton, b => b.Name.Instanced.Equals(plan.Scalp, StringComparison.OrdinalIgnoreCase));
        if (scalp < 0)
        {
            throw new InvalidOperationException($"hair: no scalp bone {plan.Scalp}");
        }
        var axis = rest[scalp].Position;
        var verts = mesh.LODModels[0].VertexBufferGPUSkin.VertexData.Select(v => v.Position).ToArray();
        foreach (var s in plan.Sectors)
        {
            var near = verts.Where(p => MathF.Abs(AngleBetween(Azimuth(p, axis), s.Az)) <= plan.SectorWidthDeg / 2f && p.Z <= plan.RootZ + plan.BandCm).ToList();
            if (near.Count < 10)
            {
                throw new InvalidOperationException($"hair: sector {s.Name} ({s.Az} degrees) has {near.Count} vertices below the root");
            }
            float tipZ = near.Min(p => p.Z);
            var chain = new HairPlan.Chain { Sector = s };
            // Joints evenly in height from the root to one segment above the tip: n joints, n segments, the last ending at the tip.
            float seg = (plan.RootZ - tipZ) / plan.Joints;
            for (int j = 0; j <= plan.Joints; ++j)
            {
                float z = plan.RootZ - j * seg;
                var band = near.Where(p => MathF.Abs(p.Z - z) <= plan.BandCm / 2f).ToList();
                if (band.Count == 0)
                {
                    band = near.OrderBy(p => MathF.Abs(p.Z - z)).Take(8).ToList();
                }
                var c = band.Aggregate(Vector3.Zero, (a, p) => a + p) / band.Count;
                c.Z = z;
                if (j < plan.Joints)
                {
                    chain.Joints.Add(c);
                }
                else
                {
                    chain.Tip = c;
                }
            }
            for (int j = 0; j < chain.Joints.Count; ++j)
            {
                string name = $"{plan.Prefix}{s.Name}_{j + 1}";
                chain.Bones.Add(name);
                plan.Generated.Add(new BonePlan
                {
                    Name = name,
                    Parent = j == 0 ? plan.Scalp : chain.Bones[j - 1],
                    At = new[] { chain.Joints[j].X, chain.Joints[j].Y, chain.Joints[j].Z },
                });
            }
            plan.Chains.Add(chain);
            report.Line($"hair chain {s.Name} at {s.Az} degrees: {near.Count} vertices, root z {plan.RootZ:F0} to tip z {tipZ:F1}, joints " +
                string.Join(" ", chain.Joints.Select(p => $"({p.X:F1},{p.Y:F1},{p.Z:F1})")) + $", tip ({chain.Tip.X:F1},{chain.Tip.Y:F1},{chain.Tip.Z:F1})");
        }
    }

    // Every vertex of every LOD re-weighted to the scalp and the chains.
    public static void Weight(SkeletalMesh mesh, HairPlan plan, Report report)
    {
        var rest = Skeleton.Rest(mesh);
        int Bone(string n) => Array.FindIndex(mesh.RefSkeleton, b => b.Name.Instanced.Equals(n, StringComparison.OrdinalIgnoreCase));
        int scalp = Bone(plan.Scalp);
        var axis = rest[scalp].Position;
        var chains = plan.Chains.OrderBy(c => c.Sector.Az).ToList();
        var chainBones = chains.Select(c => c.Bones.Select(Bone).ToArray()).ToList();
        for (int l = 0; l < mesh.LODModels.Length; ++l)
        {
            var lod = mesh.LODModels[l];
            int changed = 0;
            for (int c = 0; c < lod.Chunks.Length; ++c)
            {
                var chunk = lod.Chunks[c];
                int count = chunk.NumRigidVertices + chunk.NumSoftVertices;
                var weights = new Dictionary<int, Dictionary<int, float>>();  // chunk-relative vertex -> skeleton bone -> weight
                for (int i = 0; i < count; ++i)
                {
                    var p = lod.VertexBufferGPUSkin.VertexData[chunk.BaseVertexIndex + i].Position;
                    weights[i] = VertexWeights(p, axis, plan, chains, chainBones, scalp);
                }
                // Every vertex now has several bones: all soft.
                if (chunk.NumRigidVertices > 0)
                {
                    var moved = Layout.MakeSoft(lod, c, Enumerable.Range(0, chunk.NumRigidVertices).ToList());
                    weights = weights.ToDictionary(kv => moved[kv.Key], kv => kv.Value);
                    chunk = lod.Chunks[c];
                }
                var map = chunk.BoneMap.ToList();
                foreach (var (i, w) in weights)
                {
                    var top = w.Where(kv => kv.Value > 0).OrderByDescending(kv => kv.Value).Take(4).ToList();
                    var dense = new Dictionary<int, float>();
                    foreach (var kv in top)
                    {
                        int k = map.IndexOf((ushort)kv.Key);
                        if (k < 0)
                        {
                            map.Add((ushort)kv.Key);
                            k = map.Count - 1;
                        }
                        dense[k] = kv.Value;
                    }
                    var arr = new float[map.Count];
                    foreach (var kv in dense)
                    {
                        arr[kv.Key] = kv.Value;
                    }
                    var (bones, ws) = Weights.Quantize(arr);
                    ref var g = ref lod.VertexBufferGPUSkin.VertexData[chunk.BaseVertexIndex + i];
                    g.InfluenceBones = bones;
                    g.InfluenceWeights = ws;
                    ++changed;
                }
                chunk.BoneMap = map.ToArray();
            }
            report.Line($"hair weights, LOD {l}: {changed} vertices re-weighted to {plan.Scalp} and {chains.Count} chains");
        }
    }

    private static Dictionary<int, float> VertexWeights(Vector3 p, Vector3 axis, HairPlan plan, List<HairPlan.Chain> chains, List<int[]> bones, int scalp)
    {
        var result = new Dictionary<int, float>();
        void Add(int bone, float w)
        {
            if (w <= 0f) return;
            result[bone] = result.GetValueOrDefault(bone) + w;
        }
        // Around the head: the two chains whose azimuths enclose the vertex's (sorted by azimuth, wrapping round).
        float az = Azimuth(p, axis);
        int n = chains.Count;
        int after = chains.FindIndex(c => c.Sector.Az >= az);
        int b = after < 0 ? 0 : after;            // the first chain at or past az (wrapping)
        int a = (b - 1 + n) % n;                  // the one before it
        float span = AngleBetween(chains[b].Sector.Az, chains[a].Sector.Az);
        if (span <= 0f) span += 360f;
        float into = AngleBetween(az, chains[a].Sector.Az);
        if (into < 0f) into += 360f;
        float t = span > 0f ? Math.Clamp(into / span, 0f, 1f) : 0f;
        // A gap wider than 100 degrees (the face) isn't blended across: the nearer chain takes the vertex.
        if (span > 100f)
        {
            t = into < span / 2f ? 0f : 1f;
        }
        foreach (var (chainIndex, share) in new[] { (a, 1f - t), (b, t) })
        {
            if (share <= 0f) continue;
            var chain = chains[chainIndex];
            var chainBones = bones[chainIndex];
            // Along the chain: u in segments from joint 0 (0) to the tip (Joints.Count); above joint 0 negative.
            var zs = chain.Joints.Select(j => j.Z).Append(chain.Tip.Z).ToArray();
            float seg = zs[0] - zs[1];
            float u;
            if (p.Z >= zs[0])
            {
                u = -(p.Z - zs[0]) / MathF.Max(plan.BlendAboveCm, 0.01f) * 0.5f;  // reaches -0.5 (the scalp's centre) blendAboveCm above
            }
            else
            {
                u = zs.Length - 1;
                for (int j = 0; j < zs.Length - 1; ++j)
                {
                    if (p.Z >= zs[j + 1])
                    {
                        u = j + (zs[j] - p.Z) / MathF.Max(zs[j] - zs[j + 1], 0.01f);
                        break;
                    }
                }
            }
            // Hats centred on each segment's middle (the scalp's "segment" is -1..0, centre -0.5).
            float total = 0f;
            var hat = new List<(int Bone, float W)>();
            for (int k = -1; k < chainBones.Length; ++k)
            {
                float centre = k + 0.5f;
                float w = MathF.Max(0f, 1f - MathF.Abs(u - centre));
                if (k == chainBones.Length - 1 && u > centre) w = 1f;  // past the last bone's middle: all its own
                if (k == -1 && u < centre) w = 1f;                       // above the scalp's centre: all the scalp's
                if (w > 0f) hat.Add((k < 0 ? scalp : chainBones[k], w));
                total += w;
            }
            foreach (var (bone, w) in hat)
            {
                Add(bone, share * w / total);
            }
        }
        return result;
    }
}
