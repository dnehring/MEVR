// SPDX-License-Identifier: GPL-3.0-only
// The verbs; the code that calls LegendaryExplorerCore (GPL-3.0).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using LegendaryExplorerCore;
using LegendaryExplorerCore.GameFilesystem;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.Unreal;
using LegendaryExplorerCore.Unreal.BinaryConverters;

namespace Mevr.CockpitPackage;

internal static class Verbs
{
    public const string DefaultGamePath = @"C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition\Game\ME1";

    public static int Run(Options options)
    {
        LegendaryExplorerCoreLib.InitLib(TaskScheduler.Default, s => Console.Error.WriteLine("LEC: saving failed: " + s), null, new[] { MEGame.LE1 });
        string game = options.Get("game") ?? DefaultGamePath;
        if (Directory.Exists(game))
        {
            LE1Directory.DefaultGamePath = game;
        }
        switch (options.Verb)
        {
            case "inspect":
                return Inspect(options);
            case "survey":
                return Survey(options);
            case "build":
                return Build.Run(options, game);
            case "check":
                return Check.Run(Path.GetFullPath(options.Positional.First()), options.Get("assets"), game, options.Has("raw"));
            case "toc":
                return Toc.Run(options);
            default:
                Console.Error.WriteLine("Unknown verb " + options.Verb);
                return 2;
        }
    }

    // What the game's own packages do, to copy: static mesh buffer fields, package-stored textures, MICs over a base Material.
    // survey <CookedPCConsole> [--pattern BIOA_*_ART.pcc] [--count N]
    private static int Survey(Options options)
    {
        var meshes = new Dictionary<string, int>();
        var windings = new List<double>();
        var classImports = new Dictionary<string, int>();
        int shownTextures = 0;
        int shownMics = 0;
        int count = int.Parse(options.Get("count") ?? "20");
        var files = Directory.GetFiles(options.Positional.First(), options.Get("pattern") ?? "BIOA_*_ART.pcc").OrderBy(f => f).Where(f => new FileInfo(f).Length > 500000).Take(count);
        foreach (string file in files)
        {
            using IMEPackage pcc = MEPackageHandler.OpenMEPackage(file, forceLoadFromDisk: true);
            foreach (ImportEntry import in pcc.Imports.Where(i => i.ClassName == "Class" && (i.ObjectName == "StaticMesh" || i.ObjectName == "Texture2D" || i.ObjectName == "MaterialInstanceConstant")))
            {
                string key = $"{import.InstancedFullPath} (package file {import.PackageFile})";
                classImports[key] = classImports.GetValueOrDefault(key) + 1;
            }
            foreach (ExportEntry export in pcc.Exports)
            {
                if (export.ClassName == "StaticMesh")
                {
                    StaticMesh mesh = ObjectBinary.From<StaticMesh>(export);
                    StaticMeshRenderData lod = mesh.LODModels[0];
                    string key = $"lods {mesh.LODModels.Length} tc {lod.VertexBuffer.NumTexCoords} stride {lod.VertexBuffer.Stride} full {lod.VertexBuffer.bUseFullPrecisionUVs} vb.unk {lod.VertexBuffer.unk} pos.unk {lod.PositionVertexBuffer.unk} col {lod.ColorVertexBuffer.NumVertices > 0} ver {mesh.InternalVersion} unk1 {mesh.unk1} unk7 {mesh.unk7} wire {lod.WireframeIndexBuffer.Length > 0} edges {lod.Edges.Length > 0} ext {lod.ShadowExtrusionVertexBuffer.VertexData.Length > 0} extStride {lod.ShadowExtrusionVertexBuffer.Stride} sds {lod.ShadowTriangleDoubleSided.Length > 0} raw {lod.RawTriangles.Length > 0} body {mesh.BodySetup != 0}";
                    meshes[key] = meshes.GetValueOrDefault(key) + 1;
                    windings.Add(Check.WindingAgreement(mesh));
                }
                else if (export.ClassName == "Texture2D" && shownTextures < 3)
                {
                    PropertyCollection props = export.GetProperties();
                    if (props.GetProp<BoolProperty>("NeverStream")?.Value == true && props.GetProp<NameProperty>("TextureFileCacheName") == null)
                    {
                        ++shownTextures;
                        Console.WriteLine($"package-stored texture {export.InstancedFullPath} in {Path.GetFileName(file)}: " + string.Join("; ", props.Select(Describe)));
                        var tex = ObjectBinary.From<UTexture2D>(export);
                        Console.WriteLine("   mips: " + string.Join(", ", tex.Mips.Select(m => $"{m.SizeX}x{m.SizeY} {m.StorageType} {m.CompressedSize}")) + $" unk1 {tex.Unk1} guid {tex.TextureGuid}");
                    }
                }
                else if (export.ClassName == "MaterialInstanceConstant" && shownMics < 2)
                {
                    PropertyCollection props = export.GetProperties();
                    IEntry parent = pcc.GetEntry(props.GetProp<ObjectProperty>("Parent")?.Value ?? 0);
                    if (parent != null && parent.ClassName == "Material" && props.GetProp<ArrayProperty<StructProperty>>("TextureParameterValues") != null)
                    {
                        ++shownMics;
                        Console.WriteLine($"MIC {export.InstancedFullPath} size {export.DataSize} binary {export.GetBinaryData().Length} objflags {export.ObjectFlags} over {parent.InstancedFullPath} ({parent.GetType().Name}): " + string.Join("; ", props.Select(Describe)));
                    }
                }
            }
        }
        foreach (var pair in classImports)
        {
            Console.WriteLine($"{pair.Value,6} packages import {pair.Key}");
        }
        windings.Sort();
        if (windings.Count > 0)
        {
            Console.WriteLine($"faces agreeing with their vertex normals, per mesh: min {windings[0] * 100:F1}%, median {windings[windings.Count / 2] * 100:F1}%, max {windings[^1] * 100:F1}%");
        }
        foreach (var pair in meshes.OrderByDescending(p => p.Value))
        {
            Console.WriteLine($"{pair.Value,6} meshes: {pair.Key}");
        }
        return 0;
    }

    private static int Inspect(Options options)
    {
        string path = options.Positional.First();
        using IMEPackage pcc = MEPackageHandler.OpenMEPackage(path, forceLoadFromDisk: true);
        Console.WriteLine($"{path}: game {pcc.Game}, version {pcc.Game}, flags {pcc.Flags}, compressed {pcc.IsCompressed}, {pcc.ExportCount} exports, {pcc.ImportCount} imports, {pcc.NameCount} names");
        string filter = options.Get("filter");
        foreach (ImportEntry import in pcc.Imports.Where(e => filter == null || e.InstancedFullPath.Contains(filter, StringComparison.OrdinalIgnoreCase)).Take(400))
        {
            Console.WriteLine($"import {import.UIndex} {import.ClassName} {import.InstancedFullPath} (package file {import.PackageFile})");
        }
        foreach (ExportEntry export in pcc.Exports.Where(e => filter == null || (e.ClassName + " " + e.InstancedFullPath).Contains(filter, StringComparison.OrdinalIgnoreCase)).Take(4000))
        {
            Console.WriteLine($"export {export.UIndex} {export.ClassName} {export.InstancedFullPath} size {export.DataSize} objflags {export.ObjectFlags} expflags {export.ExportFlags} pkgflags {export.PackageFlags}");
            if (options.Has("props"))
            {
                foreach (Property p in export.GetProperties())
                {
                    Console.WriteLine("    " + Describe(p));
                }
            }
            if (options.Has("bin") && export.ClassName == "StaticMesh")
            {
                var mesh = ObjectBinary.From<StaticMesh>(export);
                Console.WriteLine($"    bounds o {mesh.Bounds.Origin} e {mesh.Bounds.BoxExtent} r {mesh.Bounds.SphereRadius} body {mesh.BodySetup} ver {mesh.InternalVersion} unk1 {mesh.unk1} unk7 {mesh.unk7} thumb {mesh.ThumbnailAngle} {mesh.ThumbnailDistance} crc {mesh.HighResSourceMeshCRC} hires '{mesh.HighResSourceMeshName}' lods {mesh.LODModels.Length} kdop nodes {mesh.kDOPTreeME3UDKLE.Nodes.Length} tris {mesh.kDOPTreeME3UDKLE.Triangles.Length}");
                foreach (StaticMeshRenderData lod in mesh.LODModels)
                {
                    var vb = lod.VertexBuffer;
                    var pb = lod.PositionVertexBuffer;
                    Console.WriteLine($"    lod verts {lod.NumVertices} idx {lod.IndexBuffer.Length} wire {lod.WireframeIndexBuffer.Length} edges {lod.Edges.Length} shadowDS {lod.ShadowTriangleDoubleSided.Length} raw {lod.RawTriangles.Length}; pos stride {pb.Stride} n {pb.NumVertices} unk {pb.unk}; vb tc {vb.NumTexCoords} stride {vb.Stride} n {vb.NumVertices} full {vb.bUseFullPrecisionUVs} unk {vb.unk}; col stride {lod.ColorVertexBuffer.Stride} n {lod.ColorVertexBuffer.NumVertices}; ext stride {lod.ShadowExtrusionVertexBuffer.Stride} n {lod.ShadowExtrusionVertexBuffer.NumVertices} data {lod.ShadowExtrusionVertexBuffer.VertexData.Length}");
                    foreach (StaticMeshElement el in lod.Elements)
                    {
                        Console.WriteLine($"      element mat {el.Material} coll {el.EnableCollision}/{el.OldEnableCollision} shadow {el.bEnableShadowCasting} first {el.FirstIndex} tris {el.NumTriangles} min {el.MinVertexIndex} max {el.MaxVertexIndex} matidx {el.MaterialIndex} frags {el.Fragments.Length}");
                    }
                }
            }
            if (options.Has("bin") && export.ClassName == "MaterialInstanceConstant")
            {
                byte[] binary = export.GetBinaryData();
                Console.WriteLine($"    binary {binary.Length} bytes: {Convert.ToHexString(binary.AsSpan(0, Math.Min(binary.Length, 48)))}");
            }
            if (options.Has("bin") && export.ClassName == "Texture2D")
            {
                var tex = ObjectBinary.From<UTexture2D>(export);
                Console.WriteLine($"    unk1 {tex.Unk1} guid {tex.TextureGuid} mips: " + string.Join(", ", tex.Mips.Select(m => $"{m.SizeX}x{m.SizeY} {m.StorageType} {m.CompressedSize}")));
                // Where the bulk data says it is, against where this export's data sits in the uncompressed package.
                Console.WriteLine($"    export data at {export.DataOffset}, {export.DataSize} bytes; mip offsets: " + string.Join(", ", tex.Mips.Select(m => m.DataOffset.ToString())));
            }
        }
        return 0;
    }

    internal static string Describe(Property p)
    {
        return p switch
        {
            IntProperty i => $"{p.Name} int {i.Value}",
            FloatProperty f => $"{p.Name} float {f.Value}",
            BoolProperty b => $"{p.Name} bool {b.Value}",
            NameProperty n => $"{p.Name} name {n.Value.Instanced}",
            EnumProperty e => $"{p.Name} enum {e.EnumType} {e.Value.Instanced}",
            ObjectProperty o => $"{p.Name} object {o.Value}",
            StrProperty s => $"{p.Name} str {s.Value}",
            StructProperty s => $"{p.Name} struct {s.StructType} {{ {string.Join("; ", s.Properties.Select(Describe))} }}",
            ArrayPropertyBase a => $"{p.Name} array [{a.Count}] {{ {string.Join(" | ", a.Properties.Take(6).Select(Describe))} }}",
            _ => $"{p.Name} {p.GetType().Name}",
        };
    }
}
