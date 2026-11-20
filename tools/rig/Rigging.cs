// SPDX-License-Identifier: GPL-3.0-only
// mevr-rig rig <plan.json>: adds bones to a skeletal mesh and weights soft regions of it to them, then writes the mesh (and
// what it needs: materials, textures) into a package of ours under a new name (tools/rig/README.md).
//
// The skinning layout this edits (UE3, as LE stores it): per LOD one or more chunks; a chunk's vertices are a contiguous
// range of the GPU vertex buffer, its rigid vertices (one bone) first, then its soft ones (up to four); a vertex's bone
// bytes index the chunk's BoneMap (chunk-local), whose entries are skeleton indices; the chunk keeps a CPU copy of its
// vertices beside the GPU buffer, which must agree. RequiredBones (sorted skeleton indices, parents included) are the bones
// the LOD poses; ActiveBoneIndices the ones its vertices use.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text.Json;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.Packages.CloningImportingAndRelinking;
using LegendaryExplorerCore.Unreal;
using LegendaryExplorerCore.Unreal.BinaryConverters;

namespace Mevr.Rig;

internal sealed class BonePlan
{
    public string Name { get; set; }
    public string Parent { get; set; }
    public float[] At { get; set; }         // rest position in the mesh's space (game units)
    public float[] Toward { get; set; }     // optional: the bone's X axis points here (a chain's next joint); else the parent's axes
    public float? Yaw { get; set; }         // optional: the bone's rest axes are the mesh's turned this many degrees about up
                                            // (a soft-tissue bone's frame: X out of the body, Y right, Z up; glutes 180)
}

internal sealed class RegionPlan
{
    public string Bone { get; set; }
    public float[] Center { get; set; }     // mesh space, game units
    public float[] Radius { get; set; }     // the ellipsoid's half axes along the mesh's X, Y, Z
    public float Weight { get; set; } = 1f; // the share the bone takes at the centre
    public float Inner { get; set; } = 0.35f; // full share inside this fraction of the ellipsoid, falling smoothly to 0 at its edge
    public string[] From { get; set; }      // only the share these bones held moves (else every bone's)
}

internal sealed class Plan
{
    public string Out { get; set; }
    public List<StepPlan> Steps { get; set; } = new();
    // A generated plan (every female body of a game): a step that fails is reported and left out, the rest are built.
    public bool ContinueOnError { get; set; }
}

// One mesh of a plan: copied from `source` into the plan's package as `name`, its bones added, its soft regions or hair
// chains weighted. `bonesOf` adds an earlier step's generated hair-chain bones too, unweighted (the body a hair follows
// must carry the bones the hair stage writes: game/Hair simulates on the body, ME3's follower rule).
internal sealed class StepPlan
{
    public string Id { get; set; }
    public string Source { get; set; }
    public string Mesh { get; set; }
    public string Name { get; set; }
    public List<BonePlan> Bones { get; set; } = new();
    public List<RegionPlan> Regions { get; set; } = new();
    public HairPlan Hair { get; set; }
    public List<string> BonesOf { get; set; } = new();
    public string Materials { get; set; } = "import";  // "copy": the materials and what they need into the package
    // A body's surface for the hair (Colliders): false keeps the game's capsules alone for this body (a suit's high collar,
    // whose inside trapped the nape's hair in the bench, 2026-11-19).
    public bool Colliders { get; set; } = true;
    public float MinSphereCm { get; set; }  // Colliders.Fit's minRadius: a suit's folds (0: every sphere)
    // "auto": the breast and glute bones and their regions placed from the mesh's own surface (Placement), added to any
    // bones and regions the step lists.
    public string Place { get; set; }
    // The game objects (full paths in the game's memory) this rig stands in for, written to the package's index (<out>.json);
    // empty: the source mesh's own path.
    public List<string> StandsFor { get; set; } = new();
}

internal static class Rigging
{
    public const int MaxChunkBones = 75;  // UE3's GPU skin limit per chunk

    public static int Run(string[] args)
    {
        string planPath = args.Length > 1 ? args[1] : throw new ArgumentException("rig <plan.json>");
        var plan = ReadPlan(planPath);
        string baseDir = Path.GetDirectoryName(Path.GetFullPath(planPath));
        string Resolve(string p)
        {
            p = p.Replace("{MEVR}", Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MEVR"))
                 .Replace("{LE3}", GameFolder(args, "LE3")).Replace("{LE2}", GameFolder(args, "LE2")).Replace("{LE1}", GameFolder(args, "LE1"));
            p = Environment.ExpandEnvironmentVariables(p);
            return Path.GetFullPath(Path.IsPathRooted(p) ? p : Path.Combine(baseDir, p));
        }
        string outPath = Resolve(plan.Out);
        Directory.CreateDirectory(Path.GetDirectoryName(outPath));
        File.Delete(outPath);
        using IMEPackage pcc = MEPackageHandler.CreateAndOpenPackage(outPath, Lex.Game, forceLoadFromDisk: true);
        var done = new Dictionary<string, StepPlan>(StringComparer.OrdinalIgnoreCase);
        var index = new List<(string path, string name)>();
        int built = 0, skipped = 0, failed = 0;
        var started = System.Diagnostics.Stopwatch.StartNew();
        foreach (var step in plan.Steps)
        {
            using var source = MEPackageHandler.OpenMEPackage(Resolve(step.Source), forceLoadFromDisk: true);
            var sourceMesh = Lex.FindMesh(source, step.Mesh);
            var report = new Report();
            Placement.Result placed = null;
            if (string.Equals(step.Place, "auto", StringComparison.OrdinalIgnoreCase))
            {
                placed = Placement.Auto(ObjectBinary.From<SkeletalMesh>(sourceMesh));
                foreach (string n in placed.Notes) report.Line("place: " + n);
                if (placed.Bones.Count == 0 && step.Bones.Count == 0 && step.Hair == null)
                {
                    // Nothing of ours to add (a sleeve, a shoulder piece): the game's own mesh stays.
                    Console.WriteLine($"{Path.GetFileNameWithoutExtension(outPath)}.{step.Name}: skipped, no soft tissue");
                    report.Print();
                    ++skipped;
                    continue;
                }
            }
            ExportEntry export = null;
            try
            {
                export = Port.Mesh(sourceMesh, pcc, step.Name, report);
                RigStep(pcc, step, sourceMesh, export, placed, done, outPath, report);
            }
            catch (Exception e) when (plan.ContinueOnError)
            {
                // A generated plan (every female body of a game) keeps going: the failed mesh is taken out again.
                if (export != null)
                {
                    EntryPruner.TrashEntryAndDescendants(export);
                }
                Console.WriteLine($"{Path.GetFileNameWithoutExtension(outPath)}.{step.Name}: FAILED, {e.GetType().Name}: {e.Message}");
                report.Print();
                ++failed;
                continue;
            }
            ++built;
            foreach (string p in step.StandsFor.Count > 0 ? step.StandsFor : new List<string> { sourceMesh.InstancedFullPath })
            {
                index.Add((p, step.Name));
            }
        }
        pcc.Save();
        string indexPath = Path.ChangeExtension(outPath, ".json");
        WriteIndex(indexPath, Path.GetFileNameWithoutExtension(outPath), index);
        Console.WriteLine($"{Path.GetFileName(outPath)}: {built} built, {skipped} skipped (no soft tissue), {failed} failed, " +
            $"{new FileInfo(outPath).Length / 1024} KB, {started.Elapsed.TotalSeconds:F0} s; index {indexPath}");
        return failed > 0 ? 1 : 0;
    }

    // <package>.json beside the package (mevr-rig-index, version 1): each game object path the plan's rigs stand in for, and the
    // rig's name in the package (game/Hair's stand-ins by path: docs/trilogy-hair-plan.md section 12, step 3).
    static void WriteIndex(string path, string package, List<(string path, string name)> index)
    {
        var o = new System.Text.StringBuilder();
        o.Append("{\n  \"format\": \"mevr-rig-index\",\n  \"version\": 1,\n  \"package\": ").Append(JsonSerializer.Serialize(package)).Append(",\n  \"meshes\": [\n");
        for (int i = 0; i < index.Count; ++i)
        {
            o.Append("    { \"path\": ").Append(JsonSerializer.Serialize(index[i].path)).Append(", \"name\": ").Append(JsonSerializer.Serialize(index[i].name))
             .Append(i + 1 < index.Count ? " },\n" : " }\n");
        }
        o.Append("  ]\n}\n");
        File.WriteAllText(path, o.ToString());
    }

    static void RigStep(IMEPackage pcc, StepPlan step, ExportEntry sourceMesh, ExportEntry export, Placement.Result placed,
        Dictionary<string, StepPlan> done, string outPath, Report report)
    {
        {
            var mesh = ObjectBinary.From<SkeletalMesh>(export);
            if (string.Equals(step.Materials, "copy", StringComparison.OrdinalIgnoreCase))
            {
                Port.CopyMaterials(sourceMesh, pcc, mesh, report);
            }
            else
            {
                Port.Materials(sourceMesh, pcc, mesh, report);
            }

            Skeleton.Check(mesh, report);
            var bones = new List<BonePlan>(step.Bones);
            var regions = new List<RegionPlan>(step.Regions);
            if (placed != null)
            {
                bones.AddRange(placed.Bones);
                regions.AddRange(placed.Regions);
            }
            if (step.Hair != null)
            {
                HairChains.Plan(mesh, step.Hair, report);
                bones.AddRange(step.Hair.Generated);
            }
            foreach (string id in step.BonesOf)
            {
                if (!done.TryGetValue(id, out var earlier) || earlier.Hair == null)
                {
                    throw new InvalidOperationException($"step {step.Id}: bonesOf '{id}' isn't an earlier step with hair chains");
                }
                bones.AddRange(earlier.Hair.Generated);
            }
            var added = AddBones(pcc, mesh, bones, report);
            if (step.Hair != null)
            {
                HairChains.Weight(mesh, step.Hair, report);
            }
            foreach (var region in regions)
            {
                int bone = Array.FindIndex(mesh.RefSkeleton, b => b.Name.Instanced.Equals(region.Bone, StringComparison.OrdinalIgnoreCase));
                if (bone < 0)
                {
                    throw new InvalidOperationException($"region: no bone {region.Bone}");
                }
                Weights.ApplyRegion(mesh, region, bone, report);
            }
            Skeleton.UpdateLodBones(mesh, added, report);
            Skeleton.Check(mesh, report);
            export.WriteBinary(mesh);
            if (step.Hair == null && step.Colliders)
            {
                // A body: its surface for the hair simulation (Colliders, the game's core/ik/BodySpheres).
                Colliders.Write(Colliders.FileFor(outPath, step.Name), step.Name, Colliders.Fit(mesh, report, step.MinSphereCm));
            }
            Console.WriteLine($"{Path.GetFileNameWithoutExtension(outPath)}.{step.Name}: {mesh.RefSkeleton.Length} bones ({added.Count} added)");
            report.Print();
            if (step.Id != null)
            {
                done[step.Id] = step;
            }
        }
    }

    public static Plan ReadPlan(string planPath) =>
        JsonSerializer.Deserialize<Plan>(File.ReadAllText(planPath), new JsonSerializerOptions { PropertyNameCaseInsensitive = true, ReadCommentHandling = JsonCommentHandling.Skip, AllowTrailingCommas = true });

    // A game's BioGame folder: --game-root <Mass Effect Legendary Edition folder>, else MEVR_GAME_ROOT, else Steam's default.
    public static string GameFolder(string[] args, string game)
    {
        string root = Program.Arg(args, "--game-root") ?? Environment.GetEnvironmentVariable("MEVR_GAME_ROOT")
            ?? @"C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition";
        string folder = game switch { "LE1" => "ME1", "LE2" => "ME2", _ => "ME3" };
        return Path.Combine(root, "Game", folder, "BioGame");
    }

    public static List<int> AddBones(IMEPackage pcc, SkeletalMesh mesh, IEnumerable<BonePlan> bonePlans, Report report)
    {
        var added = new List<int>();
        var rest = Skeleton.Rest(mesh);
        foreach (var b in bonePlans)
        {
            if (mesh.RefSkeleton.Any(x => x.Name.Instanced.Equals(b.Name, StringComparison.OrdinalIgnoreCase)))
            {
                throw new InvalidOperationException($"the mesh already has a bone {b.Name}");
            }
            int parent = Array.FindIndex(mesh.RefSkeleton, x => x.Name.Instanced.Equals(b.Parent, StringComparison.OrdinalIgnoreCase));
            if (parent < 0)
            {
                throw new InvalidOperationException($"bone {b.Name}: no parent {b.Parent}");
            }
            var at = new Vector3(b.At[0], b.At[1], b.At[2]);
            Quaternion world = rest[parent].Rotation;
            if (b.Yaw.HasValue)
            {
                world = Quaternion.CreateFromAxisAngle(Vector3.UnitZ, b.Yaw.Value * MathF.PI / 180f);
            }
            else if (b.Toward != null)
            {
                var dir = Vector3.Normalize(new Vector3(b.Toward[0], b.Toward[1], b.Toward[2]) - at);
                var x = Vector3.Transform(Vector3.UnitX, world);
                world = Quaternion.Normalize(Skeleton.Swing(x, dir) * world);
            }
            var parentInv = Quaternion.Conjugate(rest[parent].Rotation);
            var bone = new MeshBone
            {
                Name = NameReference.FromInstancedString(b.Name),
                Flags = 0,
                Orientation = Quaternion.Normalize(parentInv * world),
                Position = Vector3.Transform(at - rest[parent].Position, parentInv),
                NumChildren = 0,
                ParentIndex = parent,
                BoneColor = mesh.RefSkeleton[parent].BoneColor,
            };
            pcc.FindNameOrAdd(b.Name);
            mesh.RefSkeleton[parent].NumChildren++;
            mesh.RefSkeleton = mesh.RefSkeleton.Append(bone).ToArray();
            int index = mesh.RefSkeleton.Length - 1;
            mesh.NameIndexMap.Add(bone.Name, index);
            rest.Add(new Skeleton.Pose(world, at));
            added.Add(index);
        }
        mesh.SkeletalDepth = Skeleton.Depth(mesh);
        report.Line($"bones: {added.Count} added, depth {mesh.SkeletalDepth}");
        return added;
    }
}

internal sealed class Report
{
    private readonly List<string> lines = new();
    public void Line(string s) => lines.Add(s);
    public void Print()
    {
        foreach (var l in lines)
        {
            Console.WriteLine("  " + l);
        }
    }
}

internal static class Skeleton
{
    public readonly record struct Pose(Quaternion Rotation, Vector3 Position);

    // Component-space rest pose: W_b = W_p * L_b, P_b = P_p + W_p(t_b) (tools/motion/README.md; checked on Miranda).
    public static List<Pose> Rest(SkeletalMesh mesh)
    {
        var poses = new List<Pose>(mesh.RefSkeleton.Length);
        for (int i = 0; i < mesh.RefSkeleton.Length; ++i)
        {
            var b = mesh.RefSkeleton[i];
            if (i == 0)
            {
                poses.Add(new Pose(b.Orientation, b.Position));
                continue;
            }
            var p = poses[b.ParentIndex];
            poses.Add(new Pose(Quaternion.Normalize(p.Rotation * b.Orientation), p.Position + Vector3.Transform(b.Position, p.Rotation)));
        }
        return poses;
    }

    public static Quaternion Swing(Vector3 from, Vector3 to)
    {
        from = Vector3.Normalize(from);
        to = Vector3.Normalize(to);
        float c = Vector3.Dot(from, to);
        var axis = Vector3.Cross(from, to);
        if (axis.Length() < 1e-6f)
        {
            if (c > 0)
            {
                return Quaternion.Identity;
            }
            var other = MathF.Abs(from.X) < 0.9f ? Vector3.UnitX : Vector3.UnitY;
            return Quaternion.CreateFromAxisAngle(Vector3.Normalize(Vector3.Cross(from, other)), MathF.PI);
        }
        return Quaternion.CreateFromAxisAngle(Vector3.Normalize(axis), MathF.Acos(Math.Clamp(c, -1f, 1f)));
    }

    public static int Depth(SkeletalMesh mesh)
    {
        int max = 0;
        for (int i = 0; i < mesh.RefSkeleton.Length; ++i)
        {
            int d = 1;
            for (int j = i; j != 0; j = mesh.RefSkeleton[j].ParentIndex)
            {
                ++d;
            }
            max = Math.Max(max, d);
        }
        return max;
    }

    // Every LOD poses the added bones (RequiredBones, sorted, parents included) and lists those its vertices use (active).
    public static void UpdateLodBones(SkeletalMesh mesh, List<int> added, Report report)
    {
        for (int l = 0; l < mesh.LODModels.Length; ++l)
        {
            var lod = mesh.LODModels[l];
            var required = new SortedSet<int>(lod.RequiredBones.Select(b => (int)b));
            foreach (int b in added)
            {
                for (int j = b; ; j = mesh.RefSkeleton[j].ParentIndex)
                {
                    required.Add(j);
                    if (j == 0)
                    {
                        break;
                    }
                }
            }
            if (required.Max > 255)
            {
                throw new InvalidOperationException("RequiredBones holds bytes: more than 256 bones");
            }
            lod.RequiredBones = required.Select(b => (byte)b).ToArray();
            var active = new SortedSet<int>(lod.ActiveBoneIndices.Select(b => (int)b));
            foreach (var chunk in lod.Chunks)
            {
                foreach (ushort b in chunk.BoneMap)
                {
                    active.Add(b);
                }
            }
            lod.ActiveBoneIndices = active.Select(b => (ushort)b).ToArray();
            report.Line($"LOD {l}: required {lod.RequiredBones.Length}, active {lod.ActiveBoneIndices.Length}, chunk bones {string.Join("/", lod.Chunks.Select(c => c.BoneMap.Length))}");
        }
    }

    // LE keeps no CPU copies of the chunks' vertices (RigidVertices and SoftVertices are empty): the GPU buffer is the only
    // copy, rigid vertices with their one bone at 255. Every vertex's weights sum to 255, every bone byte is inside its
    // chunk's map, a rigid vertex has one influence, chunks tile the buffer.
    public static void Check(SkeletalMesh mesh, Report report)
    {
        for (int l = 0; l < mesh.LODModels.Length; ++l)
        {
            var lod = mesh.LODModels[l];
            var data = lod.VertexBufferGPUSkin.VertexData;
            int bad = 0;
            long covered = 0;
            foreach (var chunk in lod.Chunks)
            {
                if (chunk.BoneMap.Length > Rigging.MaxChunkBones)
                {
                    throw new InvalidOperationException($"LOD {l}: a chunk uses {chunk.BoneMap.Length} bones, over {Rigging.MaxChunkBones}");
                }
                if ((chunk.RigidVertices?.Length ?? 0) > 0 || (chunk.SoftVertices?.Length ?? 0) > 0)
                {
                    throw new InvalidOperationException($"LOD {l}: a chunk has CPU vertex copies; this tool only edits the GPU buffer");
                }
                int count = chunk.NumRigidVertices + chunk.NumSoftVertices;
                covered += count;
                for (int i = 0; i < count; ++i)
                {
                    var g = data[chunk.BaseVertexIndex + i];
                    int sum = 0, used = 0;
                    for (int k = 0; k < 4; ++k)
                    {
                        if (g.InfluenceWeights[k] > 0)
                        {
                            ++used;
                            if (g.InfluenceBones[k] >= chunk.BoneMap.Length)
                            {
                                ++bad;
                            }
                        }
                        sum += g.InfluenceWeights[k];
                    }
                    if (sum != 255 || (i < chunk.NumRigidVertices && used != 1))
                    {
                        ++bad;
                    }
                }
            }
            if (covered != data.Length || covered != lod.NumVertices)
            {
                throw new InvalidOperationException($"LOD {l}: chunks cover {covered} vertices of {data.Length} ({lod.NumVertices})");
            }
            if (bad > 0)
            {
                throw new InvalidOperationException($"LOD {l}: {bad} inconsistent influences");
            }
        }
        report.Line("check: weights sum to 255, bones inside their chunks' maps, rigid vertices one bone, chunks cover the buffer");
    }
}

internal static class Weights
{
    // A soft region: each vertex inside the ellipsoid moves a share of its weight (from the listed bones, or all) to the bone.
    public static void ApplyRegion(SkeletalMesh mesh, RegionPlan r, int bone, Report report)
    {
        var center = new Vector3(r.Center[0], r.Center[1], r.Center[2]);
        var radius = new Vector3(r.Radius[0], r.Radius[1], r.Radius[2]);
        var from = r.From?.Select(n => Array.FindIndex(mesh.RefSkeleton, b => b.Name.Instanced.Equals(n, StringComparison.OrdinalIgnoreCase))).ToHashSet();
        if (from != null && from.Contains(-1))
        {
            throw new InvalidOperationException($"region {r.Bone}: a 'from' bone isn't in the skeleton");
        }
        for (int l = 0; l < mesh.LODModels.Length; ++l)
        {
            var lod = mesh.LODModels[l];
            int touched = 0, converted = 0;
            float peak = 0;
            for (int c = 0; c < lod.Chunks.Length; ++c)
            {
                var targets = new Dictionary<int, float>();  // chunk-relative vertex -> share
                var chunk = lod.Chunks[c];
                int count = chunk.NumRigidVertices + chunk.NumSoftVertices;
                for (int i = 0; i < count; ++i)
                {
                    var g = lod.VertexBufferGPUSkin.VertexData[chunk.BaseVertexIndex + i];
                    var d = (g.Position - center) / radius;
                    float t = Share(d.Length(), r.Inner) * r.Weight;
                    if (t > 0.004f)
                    {
                        targets[i] = t;
                    }
                }
                if (targets.Count == 0)
                {
                    continue;
                }
                var rigidHit = targets.Keys.Where(i => i < chunk.NumRigidVertices).ToList();
                if (rigidHit.Count > 0)
                {
                    var moved = Layout.MakeSoft(lod, c, rigidHit);
                    converted += rigidHit.Count;
                    targets = targets.ToDictionary(kv => moved[kv.Key], kv => kv.Value);
                    chunk = lod.Chunks[c];
                }
                int local = Array.IndexOf(chunk.BoneMap, (ushort)bone);
                if (local < 0)
                {
                    chunk.BoneMap = chunk.BoneMap.Append((ushort)bone).ToArray();
                    local = chunk.BoneMap.Length - 1;
                }
                foreach (var (i, t) in targets)
                {
                    ref var g = ref lod.VertexBufferGPUSkin.VertexData[chunk.BaseVertexIndex + i];
                    var w = new float[chunk.BoneMap.Length];
                    for (int k = 0; k < 4; ++k)
                    {
                        w[g.InfluenceBones[k]] += g.InfluenceWeights[k] / 255f;
                    }
                    float movable = 0;
                    for (int k = 0; k < w.Length; ++k)
                    {
                        if (from == null || from.Contains(chunk.BoneMap[k]))
                        {
                            movable += w[k];
                        }
                    }
                    float take = t * movable;
                    if (take <= 0)
                    {
                        continue;
                    }
                    for (int k = 0; k < w.Length; ++k)
                    {
                        if (from == null || from.Contains(chunk.BoneMap[k]))
                        {
                            w[k] *= 1f - t;
                        }
                    }
                    w[local] += take;
                    var (bones, weights) = Quantize(w);
                    g.InfluenceBones = bones;
                    g.InfluenceWeights = weights;
                    ++touched;
                    peak = MathF.Max(peak, take);
                }
            }
            report.Line($"region {r.Bone}, LOD {l}: {touched} vertices weighted (peak {peak:F2}), {converted} rigid made soft");
        }
    }

    // Full share inside `inner` of the radius, smoothly to none at the edge.
    public static float Share(float d, float inner)
    {
        if (d >= 1f)
        {
            return 0f;
        }
        if (d <= inner)
        {
            return 1f;
        }
        float x = (1f - d) / (1f - inner);
        return x * x * (3f - 2f * x);
    }

    // The four largest weights, in bytes summing to 255 (the largest takes the rounding).
    public static (Influences, Influences) Quantize(float[] w)
    {
        var top = Enumerable.Range(0, w.Length).Where(k => w[k] > 0).OrderByDescending(k => w[k]).Take(4).ToList();
        float sum = top.Sum(k => w[k]);
        var b = new byte[4];
        var q = new int[4];
        for (int j = 0; j < top.Count; ++j)
        {
            b[j] = (byte)top[j];
            q[j] = (int)MathF.Round(w[top[j]] / sum * 255f);
        }
        q[0] += 255 - q.Sum();
        for (int j = top.Count - 1; j > 0; --j)
        {
            if (q[j] <= 0)
            {
                q[0] += q[j];
                q[j] = 0;
                b[j] = 0;
            }
        }
        return (new Influences(b[0], b[1], b[2], b[3]), new Influences((byte)q[0], (byte)q[1], (byte)q[2], (byte)q[3]));
    }
}

internal static class Layout
{
    // Moves the listed rigid vertices of a chunk into its soft range (behind the rigid ones it keeps, before its soft ones),
    // permuting the GPU buffer's range and remapping the index buffer. Returns old chunk-relative index -> new.
    public static Dictionary<int, int> MakeSoft(StaticLODModel lod, int chunkIndex, List<int> rigid)
    {
        if ((lod.ShadowIndices?.Length ?? 0) > 0 || (lod.Edges?.Length ?? 0) > 0 || (lod.RawPointIndices?.Length ?? 0) > 0)
        {
            throw new InvalidOperationException("the LOD has shadow indices, edges or raw points: remapping them isn't written yet");
        }
        var chunk = lod.Chunks[chunkIndex];
        var set = rigid.ToHashSet();
        int nr = chunk.NumRigidVertices, ns = chunk.NumSoftVertices, total = nr + ns;
        var order = new List<int>(total);  // new position -> old
        order.AddRange(Enumerable.Range(0, nr).Where(i => !set.Contains(i)));
        order.AddRange(Enumerable.Range(0, nr).Where(set.Contains));
        order.AddRange(Enumerable.Range(nr, ns));
        var moved = new Dictionary<int, int>(total);
        for (int n = 0; n < total; ++n)
        {
            moved[order[n]] = n;
        }
        uint b = chunk.BaseVertexIndex;
        var data = lod.VertexBufferGPUSkin.VertexData;
        var old = data.Skip((int)b).Take(total).ToArray();
        for (int n = 0; n < total; ++n)
        {
            data[b + n] = old[order[n]];  // a rigid vertex already holds its one bone at 255: it needs no other change
        }
        chunk.NumRigidVertices = nr - set.Count;
        chunk.NumSoftVertices = ns + set.Count;
        for (int i = 0; i < lod.IndexBuffer.Length; ++i)
        {
            int v = lod.IndexBuffer[i] - (int)b;
            if (v >= 0 && v < total)
            {
                lod.IndexBuffer[i] = (ushort)(b + moved[v]);
            }
        }
        return moved;
    }
}
