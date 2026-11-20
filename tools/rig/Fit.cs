// SPDX-License-Identifier: GPL-3.0-only
// A skirt fitted under the glutes (tools/rig/README.md "Dress fit", docs/trilogy-hair-plan.md section 12; the owner, 2026-11-20:
// a dress hangs straight down from the glutes' widest point, so their shape never shows, and the dancers' size boost doesn't
// either). Baked into the rigged body's vertices, every LOD, before its soft-tissue bones are placed and weighted, so the
// game's boost (game/Hair) grows the fitted shape.
//
// The shape comes from a bare body of the same game (the plan's `fitReference`, HMF_ARM_NKDa: every female body shares one
// skeleton): its LOD 0 is sliced every centimetre from above the glutes to the knees; each slice's points within 24 cm of the
// middle (no arms or hands) are reduced to their convex hull, the shape a cloth stretched round the hips and thighs takes (it
// bridges the gap between the legs). A body is a skirt when its back runs straight down from the glutes (within `StraightCm`
// over 12 cm) and stays closed in the middle (a surface at the back within 1.5 cm of the middle 24 cm under the glutes);
// trousers and bare legs aren't, nor a body with a second surface inside the skirt (legs modelled under it: a coat).
//
// A skirt's vertex on the back half (the angle from straight back under 70 degrees, fading to nothing at 110) in the band from
// 10 cm above the glutes' widest point to 38 cm under it moves along the line from the slice's middle to the reference's hull
// at that angle, scaled by the hips' width ratio, plus some ease (0.5 cm at the glutes to 2.5 cm 25 cm under them: never
// skin-tight); the band fades in over the top 7 cm and out over the bottom 16 (the hem and the front are the game's). A move
// is at most 6 cm, and a vertex further than 8 cm from the fitted shape (an arm, a hand, a belt's loop) isn't touched.
// Uses Legendary Explorer's library (GPL-3.0). Reads the player's game data; writes nothing of it.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using LegendaryExplorerCore.Unreal.BinaryConverters;

namespace Mevr.Rig;

internal static class Fit
{
    const float MaxY = 24f;          // a slice's points further out are arms and hands
    const float StraightCm = 2.5f;   // a back that comes in less than this over 12 cm under the glutes runs straight down
    const float BandTop = 10f, FullTop = 3f, FullBottom = -22f, BandBottom = -38f;  // cm from the glutes' widest point
    const float MaxMove = 6f, Reach = 8f;

    internal sealed class Slice
    {
        public float Cx;               // the middle front to back
        public float HalfWidth;        // half the hips' (or thighs') width
        public float Back = float.PositiveInfinity;  // the furthest back point 3..12 cm off the middle
        public List<Vector2> Hull = new();
        public List<(Vector2 a, Vector2 b)> Segments = new();
    }

    public sealed class Profile
    {
        public float ZGlute;
        public Dictionary<int, Slice> Slices = new();
        internal Slice At(float z) => Slices.TryGetValue((int)MathF.Round(z), out var s) ? s : null;
    }

    static Dictionary<int, Slice> SliceMesh(SkeletalMesh mesh, int zFrom, int zTo)
    {
        var lod = mesh.LODModels[0];
        var data = lod.VertexBufferGPUSkin.VertexData;
        var idx = lod.IndexBuffer;
        var slices = new Dictionary<int, Slice>();
        var points = new Dictionary<int, List<Vector2>>();
        for (int z = zFrom; z <= zTo; ++z)
        {
            slices[z] = new Slice();
            points[z] = new List<Vector2>();
        }
        Span<Vector3> t = stackalloc Vector3[3];
        for (int i = 0; i + 2 < idx.Length; i += 3)
        {
            t[0] = data[idx[i]].Position; t[1] = data[idx[i + 1]].Position; t[2] = data[idx[i + 2]].Position;
            float lo = MathF.Min(t[0].Z, MathF.Min(t[1].Z, t[2].Z)), hi = MathF.Max(t[0].Z, MathF.Max(t[1].Z, t[2].Z));
            for (int z = Math.Max(zFrom, (int)MathF.Ceiling(lo)); z <= Math.Min(zTo, (int)MathF.Floor(hi)); ++z)
            {
                var cut = new List<Vector2>(2);
                for (int e = 0; e < 3; ++e)
                {
                    var p = t[e];
                    var q = t[(e + 1) % 3];
                    if ((p.Z - z) * (q.Z - z) > 0f || p.Z == q.Z) continue;
                    float f = (z - p.Z) / (q.Z - p.Z);
                    cut.Add(new Vector2(p.X + f * (q.X - p.X), p.Y + f * (q.Y - p.Y)));
                }
                if (cut.Count < 2) continue;
                var s = slices[z];
                s.Segments.Add((cut[0], cut[1]));
                foreach (var c in cut)
                {
                    if (MathF.Abs(c.Y) <= MaxY) points[z].Add(c);
                }
            }
        }
        foreach (var (z, s) in slices)
        {
            var ps = points[z];
            if (ps.Count < 3) continue;
            float minX = ps.Min(p => p.X), maxX = ps.Max(p => p.X);
            s.Cx = (minX + maxX) / 2f;
            s.HalfWidth = (ps.Max(p => p.Y) - ps.Min(p => p.Y)) / 2f;
            foreach (var p in ps)
            {
                float ay = MathF.Abs(p.Y);
                if (ay >= 3f && ay <= 12f) s.Back = MathF.Min(s.Back, p.X);
            }
            s.Hull = ConvexHull(ps);
        }
        return slices;
    }

    static List<Vector2> ConvexHull(List<Vector2> ps)
    {
        var p = ps.Distinct().OrderBy(v => v.X).ThenBy(v => v.Y).ToList();
        if (p.Count < 3) return p;
        static float Cross(Vector2 o, Vector2 a, Vector2 b) => (a.X - o.X) * (b.Y - o.Y) - (a.Y - o.Y) * (b.X - o.X);
        var h = new List<Vector2>();
        foreach (var v in p)
        {
            while (h.Count >= 2 && Cross(h[^2], h[^1], v) <= 0f) h.RemoveAt(h.Count - 1);
            h.Add(v);
        }
        int lower = h.Count + 1;
        for (int i = p.Count - 2; i >= 0; --i)
        {
            var v = p[i];
            while (h.Count >= lower && Cross(h[^2], h[^1], v) <= 0f) h.RemoveAt(h.Count - 1);
            h.Add(v);
        }
        h.RemoveAt(h.Count - 1);
        return h;
    }

    // The hull's distance from c along the unit direction d (the furthest crossing; 0: none).
    static float HullRadius(List<Vector2> hull, Vector2 c, Vector2 d)
    {
        float best = 0f;
        for (int i = 0; i < hull.Count; ++i)
        {
            var a = hull[i] - c;
            var b = hull[(i + 1) % hull.Count] - c;
            var e = b - a;
            float den = d.X * e.Y - d.Y * e.X;
            if (MathF.Abs(den) < 1e-6f) continue;
            float tRay = (a.X * e.Y - a.Y * e.X) / den;
            float u = (a.X * d.Y - a.Y * d.X) / den;
            if (tRay > 0f && u >= 0f && u <= 1f) best = MathF.Max(best, tRay);
        }
        return best;
    }

    public static Profile Reference(SkeletalMesh mesh)
    {
        var all = SliceMesh(mesh, 40, 115);
        var p = new Profile();
        float back = float.PositiveInfinity;
        for (int z = 85; z <= 105; ++z)
        {
            if (all[z].Back < back)
            {
                back = all[z].Back;
                p.ZGlute = z;
            }
        }
        p.Slices = all;
        return p;
    }

    public sealed record Verdict(bool Skirt, float K, string Why);

    public static Verdict Detect(SkeletalMesh mesh, Profile reference)
    {
        int zg = (int)reference.ZGlute;
        var s = SliceMesh(mesh, zg - 40, zg + 12);
        var at = s[zg];
        var under = s[zg - 12];
        if (at.Hull.Count < 3 || under.Hull.Count < 3) return new Verdict(false, 1f, "no surface at the hips");
        float comesIn = under.Back - at.Back;
        if (comesIn >= StraightCm) return new Verdict(false, 1f, $"the back comes in {comesIn:F1} cm under the glutes (legs or a fitted cut)");
        var low = s[zg - 24];
        bool closed = low.Segments.Any(seg =>
            (MathF.Abs(seg.a.Y) < 1.5f && seg.a.X < low.Cx - 4f) || (MathF.Abs(seg.b.Y) < 1.5f && seg.b.X < low.Cx - 4f)
            || (seg.a.Y * seg.b.Y < 0f && MathF.Min(seg.a.X, seg.b.X) < low.Cx - 4f));
        if (!closed) return new Verdict(false, 1f, $"open between the legs 24 cm under the glutes (back {comesIn:F1} cm straight)");
        int layered = 0, counted = 0;
        for (int z = zg - 30; z <= zg; ++z)
        {
            var sl = s[z];
            if (sl.Hull.Count < 3) continue;
            ++counted;
            int crossings = 0;
            foreach (var (a, b) in sl.Segments)
            {
                if ((a.Y - 6f) * (b.Y - 6f) > 0f || a.Y == b.Y) continue;
                float x = a.X + (6f - a.Y) / (b.Y - a.Y) * (b.X - a.X);
                if (x < sl.Cx - 1f) ++crossings;
            }
            if (crossings > 1) ++layered;
        }
        if (counted > 0 && layered > 0.3f * counted) return new Verdict(false, 1f, $"a second surface inside the skirt in {layered} of {counted} slices (legs under it)");
        var refAt = reference.At(zg);
        float k = refAt != null && refAt.HalfWidth > 0f ? Math.Clamp(at.HalfWidth / refAt.HalfWidth, 0.85f, 1.2f) : 1f;
        return new Verdict(true, k, $"a skirt: its back runs straight {comesIn:F1} cm over 12 cm under the glutes, closed in the middle; hips {k:F2} of the reference's");
    }

    static float Smooth(float e0, float e1, float x)
    {
        float t = Math.Clamp((x - e0) / (e1 - e0), 0f, 1f);
        return t * t * (3f - 2f * t);
    }

    // Moves a skirt's vertices onto the fitted shape; returns how many moved (every LOD) and the largest move.
    public static (int moved, float peak) Apply(SkeletalMesh mesh, Profile reference, float k)
    {
        float zg = reference.ZGlute;
        int moved = 0;
        float peak = 0f;
        foreach (var lod in mesh.LODModels)
        {
            var data = lod.VertexBufferGPUSkin.VertexData;
            var before = data.Select(g => g.Position).ToArray();
            for (int i = 0; i < data.Length; ++i)
            {
                ref var g = ref data[i];
                var v = g.Position;
                float dz = v.Z - zg;
                if (dz > BandTop || dz < BandBottom || MathF.Abs(v.Y) > MaxY) continue;
                var sl = reference.At(v.Z);
                if (sl == null || sl.Hull.Count < 3) continue;
                float wz = dz > FullTop ? Smooth(BandTop, FullTop, dz) : dz < FullBottom ? Smooth(BandBottom, FullBottom, dz) : 1f;
                var c = new Vector2(sl.Cx, 0f);
                var rel = new Vector2(v.X, v.Y) - c;
                float r = rel.Length();
                if (r < 1e-3f) continue;
                var dir = rel / r;
                float wb = Smooth(-0.342f, 0.342f, -dir.X);   // cos of the angle from straight back: 70..110 degrees fade
                if (wz * wb <= 0f) continue;
                float ease = 0.5f + 2.0f * Math.Clamp(-dz / 25f, 0f, 1f);
                float target = k * HullRadius(sl.Hull, c, dir) + ease;
                if (target <= 0f || MathF.Abs(target - r) > Reach) continue;
                float d = Math.Clamp(target - r, -MaxMove, MaxMove) * wz * wb;
                if (MathF.Abs(d) < 0.01f) continue;
                var p = c + dir * (r + d);
                g.Position = new Vector3(p.X, p.Y, v.Z);
                ++moved;
                peak = MathF.Max(peak, MathF.Abs(d));
            }
            TurnNormals(lod.IndexBuffer, before, data);
        }
        return (moved, peak);
    }

    // The shading follows the new shape: each vertex's tangent frame turned by the turn of its surface's normal (area-weighted
    // from its triangles, before and after), so the game's own smoothing and seams stay as they were.
    static void TurnNormals(ushort[] index, Vector3[] before, GPUSkinVertex[] data)
    {
        var n0 = new Vector3[data.Length];
        var n1 = new Vector3[data.Length];
        for (int i = 0; i + 2 < index.Length; i += 3)
        {
            int a = index[i], b = index[i + 1], c = index[i + 2];
            var f0 = Vector3.Cross(before[b] - before[a], before[c] - before[a]);
            var f1 = Vector3.Cross(data[b].Position - data[a].Position, data[c].Position - data[a].Position);
            n0[a] += f0; n0[b] += f0; n0[c] += f0;
            n1[a] += f1; n1[b] += f1; n1[c] += f1;
        }
        for (int v = 0; v < data.Length; ++v)
        {
            if (n0[v].LengthSquared() < 1e-12f || n1[v].LengthSquared() < 1e-12f) continue;
            var a = Vector3.Normalize(n0[v]);
            var b = Vector3.Normalize(n1[v]);
            float dot = Math.Clamp(Vector3.Dot(a, b), -1f, 1f);
            if (dot > 0.99999f) continue;
            var axis = Vector3.Cross(a, b);
            if (axis.LengthSquared() < 1e-12f) continue;
            var q = Quaternion.CreateFromAxisAngle(Vector3.Normalize(axis), MathF.Acos(dot));
            ref var g = ref data[v];
            g.TangentX = Turn(g.TangentX, q);
            g.TangentZ = Turn(g.TangentZ, q);
        }
    }

    static PackedNormal Turn(PackedNormal n, Quaternion q)
    {
        var v = new Vector3((n.X - 127.5f) / 127.5f, (n.Y - 127.5f) / 127.5f, (n.Z - 127.5f) / 127.5f);
        v = Vector3.Transform(v, q);
        static byte Pack(float f) => (byte)Math.Clamp(MathF.Round(f * 127.5f + 127.5f), 0f, 255f);
        return new PackedNormal(Pack(v.X), Pack(v.Y), Pack(v.Z), n.W);
    }
}
