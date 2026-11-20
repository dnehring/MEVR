// SPDX-License-Identifier: GPL-3.0-only
// mevr-rig inspect: a skeletal mesh's skeleton and skinning layout (LODs, chunks, bone maps, rigid and soft vertices), the
// facts a re-rig depends on.
using System;
using System.Linq;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.Unreal.BinaryConverters;

namespace Mevr.Rig;

internal static class Inspect
{
    public static int Run(string[] args)
    {
        using var package = MEPackageHandler.OpenMEPackage(args[1], forceLoadFromDisk: true);
        var export = Lex.FindMesh(package, args[2]);
        var mesh = ObjectBinary.From<SkeletalMesh>(export);
        Console.WriteLine($"{export.InstancedFullPath}: {mesh.RefSkeleton.Length} bones, depth {mesh.SkeletalDepth}, {mesh.LODModels.Length} LOD(s), {mesh.Materials.Length} material(s), origin {mesh.Origin}, rot origin {mesh.RotOrigin.Pitch},{mesh.RotOrigin.Yaw},{mesh.RotOrigin.Roll}");
        Console.WriteLine($"  name map {mesh.NameIndexMap.Count}, per-poly kDOPs {mesh.PerPolyBoneKDOPs.Length}, break names {mesh.BoneBreakNames.Length}, clothing {mesh.ClothingAssets.Length}");
        for (int l = 0; l < mesh.LODModels.Length; ++l)
        {
            var lod = mesh.LODModels[l];
            var vb = lod.VertexBufferGPUSkin;
            Console.WriteLine($"  LOD {l}: {lod.NumVertices} vertices ({vb?.VertexData?.Length} in the GPU buffer, packed position {vb?.bUsePackedPosition}, full UVs {vb?.bUseFullPrecisionUVs}), {lod.IndexBuffer.Length / 3} triangles, required bones {lod.RequiredBones.Length}, active {lod.ActiveBoneIndices.Length}, raw points {lod.RawPointIndices?.Length}, edges {lod.Edges?.Length}, shadow indices {lod.ShadowIndices?.Length}");
            foreach (var s in lod.Sections)
            {
                Console.WriteLine($"    section: material {s.MaterialIndex}, chunk {s.ChunkIndex}, {s.NumTriangles} triangles");
            }
            for (int c = 0; c < lod.Chunks.Length; ++c)
            {
                var ch = lod.Chunks[c];
                Console.WriteLine($"    chunk {c}: base vertex {ch.BaseVertexIndex}, {ch.NumRigidVertices} rigid, {ch.NumSoftVertices} soft, {ch.BoneMap.Length} bones in its map, max {ch.MaxBoneInfluences} influences; CPU copies {ch.RigidVertices?.Length} rigid, {ch.SoftVertices?.Length} soft");
            }
        }
        string dump = Program.Arg(args, "--dump");
        if (dump != null)
        {
            // Each vertex of a LOD in the mesh's space with its influences by bone name (rest pose), for placing regions.
            int l = int.Parse(Program.Arg(args, "--lod") ?? "0");
            var lod = mesh.LODModels[l];
            using var w = new System.IO.StreamWriter(dump);
            w.WriteLine("i,x,y,z,rigid,b0,w0,b1,w1,b2,w2,b3,w3");
            foreach (var chunk in lod.Chunks)
            {
                for (int i = 0; i < chunk.NumRigidVertices + chunk.NumSoftVertices; ++i)
                {
                    int v = (int)chunk.BaseVertexIndex + i;
                    var g = lod.VertexBufferGPUSkin.VertexData[v];
                    var cols = new System.Collections.Generic.List<string> { v.ToString(), F(g.Position.X), F(g.Position.Y), F(g.Position.Z), i < chunk.NumRigidVertices ? "1" : "0" };
                    for (int k = 0; k < 4; ++k)
                    {
                        // LE keeps no CPU copies of the chunks' vertices: the GPU buffer is the only one, rigid vertices too.
                        bool used = g.InfluenceWeights[k] > 0;
                        int bone = g.InfluenceBones[k];
                        cols.Add(used ? (bone < chunk.BoneMap.Length ? mesh.RefSkeleton[chunk.BoneMap[bone]].Name.Instanced : $"?{bone}") : "");
                        cols.Add(used ? g.InfluenceWeights[k].ToString() : "0");
                    }
                    w.WriteLine(string.Join(",", cols));
                }
            }
            Console.WriteLine($"  dumped LOD {l} to {dump}");
            string tris = Program.Arg(args, "--tris");
            if (tris != null)
            {
                using var t = new System.IO.StreamWriter(tris);
                t.WriteLine("a,b,c");
                for (int i = 0; i + 2 < lod.IndexBuffer.Length; i += 3)
                {
                    t.WriteLine($"{lod.IndexBuffer[i]},{lod.IndexBuffer[i + 1]},{lod.IndexBuffer[i + 2]}");
                }
                Console.WriteLine($"  its triangles to {tris}");
            }
        }
        if (args.Contains("--rest"))
        {
            var rest = Skeleton.Rest(mesh);
            for (int i = 0; i < rest.Count; ++i)
            {
                Console.WriteLine($"  rest [{i}] {mesh.RefSkeleton[i].Name.Instanced}: ({rest[i].Position.X:F1}, {rest[i].Position.Y:F1}, {rest[i].Position.Z:F1})");
            }
        }
        if (args.Contains("--bones"))
        {
            for (int i = 0; i < mesh.RefSkeleton.Length; ++i)
            {
                var b = mesh.RefSkeleton[i];
                Console.WriteLine($"  [{i}] {b.Name.Instanced} parent {b.ParentIndex} children {b.NumChildren} flags {b.Flags} pos ({b.Position.X:F2}, {b.Position.Y:F2}, {b.Position.Z:F2})");
            }
        }
        return 0;
    }

    private static string F(float v) => v.ToString("F2", System.Globalization.CultureInfo.InvariantCulture);
}
