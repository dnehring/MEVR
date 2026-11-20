// SPDX-License-Identifier: GPL-3.0-only
// The body's surface for the hair simulation (tools/rig/README.md "Body spheres"): spheres just under the skin of the neck,
// upper torso, shoulders and breasts, fitted to the mesh's own vertices and normals, each carried by the bone its vertex
// follows most. The game (core/ik/BodySpheres, game/Hair) keeps hair points out of them, so hair lies on this body instead
// of on round capsules guessed per bone (Miranda, 2026-11-19: her neck is 5 cm from its bones and her shoulders' tops 9-13 cm,
// where the guessed capsules were 13 and 7: the nape's hair sat inside one and the shoulders' passed through the other).
// Uses Legendary Explorer's library (GPL-3.0). The file is the player's game data's: local, never committed or shipped.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using LegendaryExplorerCore.Unreal.BinaryConverters;

namespace Mevr.Rig;

internal static class Colliders
{
    // The bones whose vertices the hair can reach: the neck, the chest up from the middle of the back, the collars, the
    // shoulders (with their twist bones) and the breasts. Not the arms below the shoulder or the head (the head is its own mesh).
    static bool Reached(string bone) =>
        bone.StartsWith("Neck", StringComparison.OrdinalIgnoreCase) ||
        bone.Equals("Chest1", StringComparison.OrdinalIgnoreCase) || bone.Equals("Chest2", StringComparison.OrdinalIgnoreCase) ||
        bone.EndsWith("Collar", StringComparison.OrdinalIgnoreCase) ||
        (bone.Contains("Shoulder", StringComparison.OrdinalIgnoreCase) && !bone.Contains("Swivel", StringComparison.OrdinalIgnoreCase)) ||
        bone.StartsWith("Breast", StringComparison.OrdinalIgnoreCase);

    const float MinRadius = 2.5f;   // game units (cm)
    const float MaxRadius = 7.0f;
    const float Inside = 0.85f;     // a sphere's radius at most this share of its vertex's distance from its bone's axis

    public readonly record struct Sphere(string Bone, Vector3 Centre, float Radius, Vector3 Normal);  // Normal: the skin's outward side

    // `minRadius` (game units): spheres smaller than this after the shrink are left out, a smoother surface for a body whose
    // folds made many small ones (Miranda's suit: its armour's 1.5-cm spheres rattled the hair; from 3.5 cm her walk's hair
    // was calm, 2026-11-19). 0: every sphere (a body without folds needs its small ones: the underwear body's surface had
    // 10-cm holes without them).
    public static List<Sphere> Fit(SkeletalMesh mesh, Report report, float minRadius = 0f)
    {
        var lod = mesh.LODModels[0];
        var data = lod.VertexBufferGPUSkin.VertexData;
        int count = data.Length;
        var positions = new Vector3[count];
        var bone = new int[count];
        for (int v = 0; v < count; ++v)
        {
            positions[v] = new Vector3(data[v].Position.X, data[v].Position.Y, data[v].Position.Z);
            bone[v] = -1;
        }
        // Each vertex's bone: its heaviest influence, through its chunk's bone map.
        foreach (var chunk in lod.Chunks)
        {
            for (int i = 0; i < chunk.NumRigidVertices + chunk.NumSoftVertices; ++i)
            {
                int v = (int)chunk.BaseVertexIndex + i;
                if (v >= count) continue;
                var g = data[v];
                int best = 0;
                for (int k = 1; k < 4; ++k) if (g.InfluenceWeights[k] > g.InfluenceWeights[best]) best = k;
                int local = g.InfluenceBones[best];
                // A vertex the breast moves by a sixth or more is carried by the breast bone: its sphere jiggles with her
                // (on the chest bone, the jiggling breast went through the front strands: the owner, 2026-11-19).
                for (int k = 0; k < 4; ++k)
                {
                    int b = g.InfluenceBones[k];
                    if (g.InfluenceWeights[k] >= 40 && b < chunk.BoneMap.Length &&
                        mesh.RefSkeleton[chunk.BoneMap[b]].Name.Instanced.StartsWith("Breast", StringComparison.OrdinalIgnoreCase))
                    {
                        local = b;
                        break;
                    }
                }
                bone[v] = local < chunk.BoneMap.Length ? chunk.BoneMap[local] : -1;
            }
        }
        // Normals from the triangles (area-weighted), oriented away from the vertex's bone.
        var normals = new Vector3[count];
        var index = lod.IndexBuffer;
        for (int t = 0; t + 2 < index.Length; t += 3)
        {
            int a = (int)index[t], b = (int)index[t + 1], c = (int)index[t + 2];
            if (a >= count || b >= count || c >= count) continue;
            var n = Vector3.Cross(positions[b] - positions[a], positions[c] - positions[a]);
            normals[a] += n; normals[b] += n; normals[c] += n;
        }
        var rest = Skeleton.Rest(mesh);
        // The spine's column, level: through the neck bone (God's when there's none).
        int neck = Array.FindIndex(mesh.RefSkeleton, x => x.Name.Instanced.Equals("Neck", StringComparison.OrdinalIgnoreCase));
        Vector3 column = rest[neck >= 0 ? neck : 0].Position;
        var child = new int[mesh.RefSkeleton.Length];
        Array.Fill(child, -1);
        for (int i = 1; i < mesh.RefSkeleton.Length; ++i)
        {
            int p = mesh.RefSkeleton[i].ParentIndex;
            if (p >= 0 && child[p] < 0) child[p] = i;
        }
        var candidates = new List<Sphere>();
        for (int v = 0; v < count; ++v)
        {
            int b = bone[v];
            if (b < 0 || !Reached(mesh.RefSkeleton[b].Name.Instanced) || normals[v].LengthSquared() < 1e-12f) continue;
            // The bone's axis: to its first child, else its point.
            Vector3 a0 = rest[b].Position;
            Vector3 a1 = child[b] >= 0 ? rest[child[b]].Position : a0;
            Vector3 onAxis = Closest(positions[v], a0, a1);
            Vector3 outward = positions[v] - onAxis;
            float axisDistance = outward.Length();
            if (axisDistance < 1e-3f) continue;
            Vector3 n = Vector3.Normalize(normals[v]);
            if (Vector3.Dot(n, outward) < 0f) n = -n;  // the index buffer's winding isn't known: outward is away from the bone
            float r = Math.Clamp(Inside * axisDistance, MinRadius, MaxRadius);
            if (r > Inside * axisDistance) continue;  // too thin a part here for a sphere that stays inside
            Vector3 centre = positions[v] - n * r;
            // Under the skin everywhere: shrunk until no vertex of the body is inside it (folds, the collar, the armpit: a
            // sphere tangent at one vertex cut through the surface across the fold).
            float nearest = float.MaxValue;
            for (int u = 0; u < count; ++u)
            {
                if (u == v) continue;
                float d = Vector3.Distance(centre, positions[u]);
                if (d < nearest) nearest = d;
            }
            r = Math.Min(r, nearest);
            if (r < 0.6f * MinRadius || r < minRadius) continue;
            // An inner surface (a high collar's inside, facing the neck: its normal toward the spine's column, measured
            // level): hair lies over a collar, never in it, and spheres under that face trapped the nape's hair (Miranda's
            // suit, 2026-11-19: on the bench her walk's hair never settled with them).
            var level = new Vector2(centre.X - column.X, centre.Y - column.Y);
            var levelNormal = new Vector2(n.X, n.Y);
            if (level.LengthSquared() > 1e-6f && levelNormal.LengthSquared() > 0.04f &&
                Vector2.Dot(Vector2.Normalize(levelNormal), Vector2.Normalize(level)) < -0.3f &&
                !mesh.RefSkeleton[b].Name.Instanced.StartsWith("Breast", StringComparison.OrdinalIgnoreCase)) continue;
            candidates.Add(new Sphere(mesh.RefSkeleton[b].Name.Instanced, centre, r, n));
        }
        // Fewer spheres: the largest first, a candidate kept only away from those already kept (its centre further than 0.6 of
        // the smaller radius from theirs).
        var kept = new List<Sphere>();
        foreach (var s in candidates.OrderByDescending(s => s.Bone.StartsWith("Breast", StringComparison.OrdinalIgnoreCase)).ThenByDescending(s => s.Radius))  // the jiggling breast first
        {
            bool near = false;
            foreach (var k in kept)
            {
                if (Vector3.Distance(s.Centre, k.Centre) < 0.6f * Math.Min(s.Radius, k.Radius)) { near = true; break; }
            }
            if (!near) kept.Add(s);
        }
        report.Line($"body spheres: {kept.Count} from {candidates.Count} candidates over {kept.Select(s => s.Bone).Distinct().Count()} bones " +
                    $"(radius {(kept.Count > 0 ? kept.Min(s => s.Radius) : 0):F1}..{(kept.Count > 0 ? kept.Max(s => s.Radius) : 0):F1} cm)");
        return kept;
    }

    static Vector3 Closest(Vector3 p, Vector3 a, Vector3 b)
    {
        Vector3 ab = b - a;
        float l2 = ab.LengthSquared();
        if (l2 < 1e-9f) return a;
        float t = Math.Clamp(Vector3.Dot(p - a, ab) / l2, 0f, 1f);
        return a + ab * t;
    }

    // The game's file: %LOCALAPPDATA%\MEVR\rig\<GAME>\colliders\<mesh>.json (core/ik/BodySpheres reads it).
    public static void Write(string path, string meshName, List<Sphere> spheres)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path));
        var ci = CultureInfo.InvariantCulture;
        var sb = new StringBuilder();
        sb.Append($"{{\"format\": \"mevr-body-spheres\", \"version\": 1, \"mesh\": \"{meshName}\", \"units\": \"game, the mesh's bind pose\", \"spheres\": [");
        for (int i = 0; i < spheres.Count; ++i)
        {
            var s = spheres[i];
            sb.Append(i == 0 ? "\n  " : ",\n  ");
            sb.Append(string.Format(ci, "{{\"bone\": \"{0}\", \"c\": [{1:F2}, {2:F2}, {3:F2}], \"r\": {4:F2}, \"n\": [{5:F3}, {6:F3}, {7:F3}]}}", s.Bone, s.Centre.X, s.Centre.Y, s.Centre.Z, s.Radius, s.Normal.X, s.Normal.Y, s.Normal.Z));
        }
        sb.Append("\n]}\n");
        File.WriteAllText(path, sb.ToString());
    }

    public static string FileFor(string outPackage, string meshName) =>
        Path.Combine(Path.GetDirectoryName(Path.GetFullPath(outPackage)), "colliders", meshName + ".json");

    // `mevr-rig colliders <package.pcc> <mesh> [--out file.json] [--min-radius cm]`: the spheres of a mesh already built (or any
    // body).
    public static int Run(string[] args)
    {
        using var package = LegendaryExplorerCore.Packages.MEPackageHandler.OpenMEPackage(args[1], forceLoadFromDisk: true);
        var export = Lex.FindMesh(package, args[2]);
        var mesh = ObjectBinary.From<SkeletalMesh>(export);
        var report = new Report();
        float min = float.Parse(Program.Arg(args, "--min-radius") ?? "0", CultureInfo.InvariantCulture);
        var spheres = Fit(mesh, report, min);
        string output = Program.Arg(args, "--out") ?? FileFor(args[1], export.ObjectName.Instanced);
        Write(output, export.ObjectName.Instanced, spheres);
        report.Print();
        Console.WriteLine($"{export.ObjectName.Instanced}: {spheres.Count} spheres -> {output}");
        return 0;
    }
}
