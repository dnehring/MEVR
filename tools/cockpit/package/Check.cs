// SPDX-License-Identifier: GPL-3.0-only
// Reopens a cockpit package and compares it with its models and with the game's own packages.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.Textures;
using LegendaryExplorerCore.Unreal;
using LegendaryExplorerCore.Unreal.BinaryConverters;
using SharpGLTF.Schema2;
using LecImage = LegendaryExplorerCore.Textures.Image;
using PixelFormat = LegendaryExplorerCore.Textures.PixelFormat;

namespace Mevr.CockpitPackage;

internal static class Check
{
    private static int failures;

    private static void Report(bool ok, string line)
    {
        Console.WriteLine($"  {(ok ? "ok  " : "FAIL")} {line}");
        failures += ok ? 0 : 1;
    }

    private static Vector3 Unpack(PackedNormal n) => new((n.X - 127.5f) / 127.5f, (n.Y - 127.5f) / 127.5f, (n.Z - 127.5f) / 127.5f);

    // Which way the triangles wind against their normals: the share of faces whose geometric normal agrees with the vertex normal.
    public static double WindingAgreement(StaticMesh mesh)
    {
        StaticMeshRenderData lod = mesh.LODModels[0];
        Vector3[] p = lod.PositionVertexBuffer.VertexData;
        int agree = 0;
        int counted = 0;
        for (int i = 0; i + 2 < lod.IndexBuffer.Length; i += 3)
        {
            int a = lod.IndexBuffer[i];
            int b = lod.IndexBuffer[i + 1];
            int c = lod.IndexBuffer[i + 2];
            Vector3 face = Vector3.Cross(p[b] - p[a], p[c] - p[a]);
            if (face.LengthSquared() < 1e-12f)
            {
                continue;
            }
            Vector3 normal = Unpack(lod.VertexBuffer.VertexData[a].TangentZ) + Unpack(lod.VertexBuffer.VertexData[b].TangentZ) + Unpack(lod.VertexBuffer.VertexData[c].TangentZ);
            ++counted;
            agree += Vector3.Dot(face, normal) > 0.0f ? 1 : 0;
        }
        return counted == 0 ? 0.5 : (double)agree / counted;
    }

    private static readonly string[] GlobalPackages = { "Core", "Engine", "SFXGame", "Startup_INT" };

    // Every import against the packages the game keeps loaded: Core, Engine, SFXGame by their own name, and the forced
    // exports of Startup_INT (EngineMaterials.* lives there, there is no EngineMaterials.pcc).
    private static void CheckImports(IMEPackage pcc, string gamePath)
    {
        string cooked = Path.Combine(gamePath, "BioGame", "CookedPCConsole");
        if (!Directory.Exists(cooked))
        {
            Console.WriteLine("  (the game isn't here: imports not checked)");
            return;
        }
        var known = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var classes = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (string name in GlobalPackages)
        {
            using IMEPackage global = MEPackageHandler.OpenMEPackage(Path.Combine(cooked, name + ".pcc"), forceLoadFromDisk: true);
            bool isStartup = name == "Startup_INT";
            foreach (ExportEntry export in global.Exports)
            {
                string path = export.IsForcedExport || isStartup ? export.InstancedFullPath : $"{name}.{export.InstancedFullPath}";
                known.Add(path);
                classes[path] = export.ClassName;
                // The outer packages a path needs.
                string[] parts = path.Split('.');
                known.Add(parts[0]);
            }
            // Native classes with no script export of their own (Engine.StaticMesh): the game's packages import them by this name.
            foreach (ImportEntry import in global.Imports.Where(i => i.ClassName == "Class"))
            {
                known.Add(import.InstancedFullPath);
            }
            known.Add(name);
        }
        foreach (ImportEntry import in pcc.Imports)
        {
            string path = import.InstancedFullPath;
            bool found = known.Contains(path);
            bool classMatches = !classes.TryGetValue(path, out string cls) || cls == import.ClassName;
            Report(found && classMatches, $"import {import.ClassName} {path}: {(found ? classMatches ? "in the game's always-loaded packages" : "there, as a " + cls : "NOT FOUND in " + string.Join(", ", GlobalPackages))}");
        }
    }

    public static int Run(string path, string assets, string gamePath)
    {
        failures = 0;
        Console.WriteLine($"Checking {path}");
        using IMEPackage pcc = MEPackageHandler.OpenMEPackage(path, forceLoadFromDisk: true);
        Console.WriteLine($"  game {pcc.Game}, flags {pcc.Flags}, {pcc.ExportCount} exports, {pcc.ImportCount} imports, {pcc.NameCount} names, {new FileInfo(path).Length / 1048576.0:F1} MB");
        foreach (var property in pcc.GetType().GetProperties().Where(p => p.Name.Contains("Compress", StringComparison.OrdinalIgnoreCase) && p.GetIndexParameters().Length == 0))
        {
            Console.WriteLine($"  {property.Name}: {property.GetValue(pcc)}");
        }
        Report(pcc.Game == MEGame.LE1, "an LE1 package");
        Report(pcc.Flags.HasFlag(UnrealFlags.EPackageFlags.Cooked), "flagged cooked");
        foreach (ExportEntry export in pcc.Exports)
        {
            Console.WriteLine($"  export {export.UIndex} {export.ClassName} {pcc.FileNameNoExtension}.{export.InstancedFullPath}, {export.DataSize} bytes");
        }
        CheckImports(pcc, gamePath);

        foreach (ExportEntry export in pcc.Exports.Where(e => e.ClassName == "Texture2D"))
        {
            var texture = ObjectBinary.From<UTexture2D>(export);
            PropertyCollection props = export.GetProperties();
            Report(texture.Mips.All(m => m.StorageType == StorageTypes.pccUnc), $"{export.ObjectName.Instanced}: all {texture.Mips.Count} mips stored in the package, uncompressed");
            Report(props.GetProp<BoolProperty>("NeverStream")?.Value == true || texture.Mips.Count < 6, $"{export.ObjectName.Instanced}: NeverStream {props.GetProp<BoolProperty>("NeverStream")?.Value}");
            Report(props.GetProp<EnumProperty>("Format")?.Value.Instanced == "PF_DXT1", $"{export.ObjectName.Instanced}: {props.GetProp<EnumProperty>("Format")?.Value.Instanced}, {texture.Mips[0].SizeX}x{texture.Mips[0].SizeY}, LODGroup {props.GetProp<EnumProperty>("LODGroup")?.Value.Instanced}, InternalFormatLODBias {props.GetProp<IntProperty>("InternalFormatLODBias")?.Value.ToString() ?? "-"}");
            // The sizes as the game's own DXT textures hold them: halving, but never below a 4x4 block.
            Report(texture.Mips.Select((m, i) => m.SizeX == Math.Max(4, texture.Mips[0].SizeX >> i) && m.SizeY == Math.Max(4, texture.Mips[0].SizeY >> i)
                && m.Mip.Length == m.SizeX / 4 * (m.SizeY / 4) * 8).All(ok => ok),
                $"{export.ObjectName.Instanced}: each mip half the last down to a 4x4 block, each of its DXT1 size; GUID {texture.TextureGuid}");
            // The bulk data's offset lies inside the export, as the game's own (absolute in the uncompressed package).
            Report(texture.Mips.Where(m => m.StorageType == StorageTypes.pccUnc).All(m => m.DataOffset > export.DataOffset && m.DataOffset < export.DataOffset + export.DataSize),
                $"{export.ObjectName.Instanced}: mip offsets inside the export ({export.DataOffset}..{export.DataOffset + export.DataSize}): first {texture.Mips[0].DataOffset}");
            // The mean colour survived the compression with its channels in the right order.
            int top = texture.Mips.FindIndex(m => m.SizeX <= 64);
            var mip = texture.Mips[top];
            int w = mip.SizeX;
            int h = mip.SizeY;
            byte[] bgra = LecImage.convertRawToARGB(mip.Mip, ref w, ref h, PixelFormat.DXT1);
            Vector3 mean = Build.MeanRgb(bgra, 4);
            Console.WriteLine($"       {export.ObjectName.Instanced}: mean RGB at {w}x{h}: {mean.X:F1} {mean.Y:F1} {mean.Z:F1}");
        }
        foreach (ExportEntry export in pcc.Exports.Where(e => e.ClassName == "MaterialInstanceConstant"))
        {
            PropertyCollection props = export.GetProperties();
            IEntry parent = pcc.GetEntry(props.GetProp<ObjectProperty>("Parent")?.Value ?? 0);
            StructProperty value = props.GetProp<ArrayProperty<StructProperty>>("TextureParameterValues")?.FirstOrDefault();
            IEntry texture = pcc.GetEntry(value?.GetProp<ObjectProperty>("ParameterValue")?.Value ?? 0);
            Report(parent != null && parent.InstancedFullPath == $"{Build.ParentPackage}.{Build.ParentMaterial}" && parent.ClassName == "Material" && texture?.ClassName == "Texture2D"
                && value.GetProp<NameProperty>("ParameterName")?.Value.Instanced == Build.ParameterName,
                $"{export.ObjectName.Instanced}: parent {parent?.InstancedFullPath}, {value?.GetProp<NameProperty>("ParameterName")?.Value.Instanced} = {texture?.ObjectName.Instanced}, binary {export.GetBinaryData().Length} bytes");
        }

        Dictionary<string, ModelSource> sources = assets != null ? Build.Sources(assets).ToDictionary(s => s.Name) : new Dictionary<string, ModelSource>();
        foreach (ExportEntry export in pcc.Exports.Where(e => e.ClassName == "StaticMesh"))
        {
            StaticMesh mesh = ObjectBinary.From<StaticMesh>(export);
            StaticMeshRenderData lod = mesh.LODModels[0];
            string name = export.ObjectName.Instanced;
            bool buffers = lod.NumVertices == lod.PositionVertexBuffer.VertexData.Length && lod.NumVertices == lod.VertexBuffer.VertexData.Length
                && lod.IndexBuffer.All(i => i < lod.NumVertices) && lod.IndexBuffer.Length % 3 == 0;
            Report(buffers, $"{name}: {lod.NumVertices} vertices, {lod.IndexBuffer.Length / 3} triangles, buffers agree, every index in range; UVs {lod.VertexBuffer.NumTexCoords} stride {lod.VertexBuffer.Stride}");
            int elementTriangles = lod.Elements.Sum(e => (int)e.NumTriangles);
            Report(elementTriangles == lod.IndexBuffer.Length / 3 && lod.Elements.All(e => pcc.IsUExport(e.Material)), $"{name}: elements {string.Join(", ", lod.Elements.Select(e => $"{pcc.GetEntry(e.Material)?.ObjectName.Instanced} x {e.NumTriangles}"))}");
            Report(mesh.unk1 == mesh.LODModels.Length && mesh.InternalVersion == 19, $"{name}: LOD count field {mesh.unk1}, internal version {mesh.InternalVersion} (as the game's meshes)");
            Vector3[] p = lod.PositionVertexBuffer.VertexData;
            Vector3 lo = p.Aggregate(new Vector3(float.MaxValue), Vector3.Min);
            Vector3 hi = p.Aggregate(new Vector3(float.MinValue), Vector3.Max);
            Console.WriteLine($"       {name}: extent {lo} .. {hi} units; stored bounds origin {mesh.Bounds.Origin} extent {mesh.Bounds.BoxExtent} radius {mesh.Bounds.SphereRadius}");
            Report(Enumerable.Range(0, 3).All(a => lo[a] >= mesh.Bounds.Origin[a] - mesh.Bounds.BoxExtent[a] - 0.01f && hi[a] <= mesh.Bounds.Origin[a] + mesh.Bounds.BoxExtent[a] + 0.01f), $"{name}: the vertices are inside the stored bounds");
            double winding = WindingAgreement(mesh);
            Console.WriteLine($"       {name}: faces agreeing with their vertex normals: {winding * 100.0:F1}%");

            // A model too big for one mesh is split by material: <name> the textured primitive, <name>_Backing the other.
            bool isBacking = name.EndsWith(Build.BackingSuffix, StringComparison.Ordinal);
            string modelName = isBacking ? name[..^Build.BackingSuffix.Length] : name;
            bool isSplit = isBacking || pcc.Exports.Any(e => e.ClassName == "StaticMesh" && e.ObjectName.Instanced == name + Build.BackingSuffix);
            if (!sources.TryGetValue(modelName, out ModelSource source))
            {
                continue;
            }
            // Corner by corner against the file: glTF (x, y, z) x 100 is (x, z, y), UVs as they are.
            ModelRoot model = ModelRoot.Load(source.Path, SharpGLTF.Validation.ValidationMode.Skip);
            var corners = new List<(Vector3 Position, Vector2 Uv)>();
            int fileVertices = 0;
            foreach (MeshPrimitive primitive in model.LogicalNodes.First(n => n.Mesh != null).Mesh.Primitives)
            {
                bool textured = primitive.Material?.FindChannel("BaseColor")?.Texture != null;
                if (isSplit && textured == isBacking)
                {
                    continue;
                }
                var positions = primitive.GetVertexAccessor("POSITION").AsVector3Array();
                var uvs = primitive.GetVertexAccessor("TEXCOORD_0").AsVector2Array();
                fileVertices += positions.Count;
                foreach ((int a, int b, int c) in primitive.GetTriangleIndices())
                {
                    corners.Add((positions[a], uvs[a]));
                    corners.Add((positions[b], uvs[b]));
                    corners.Add((positions[c], uvs[c]));
                }
            }
            Report(corners.Count == lod.IndexBuffer.Length, $"{name}: {corners.Count} corners in the glb, {lod.IndexBuffer.Length} indices in the mesh ({fileVertices} glb vertices welded to {lod.NumVertices})");
            int wrongPlace = 0;
            int wrongUv = 0;
            double worstUv = 0.0;
            for (int i = 0; i < corners.Count && i < lod.IndexBuffer.Length; ++i)
            {
                int index = lod.IndexBuffer[i];
                Vector3 expected = new Vector3(corners[i].Position.X, corners[i].Position.Z, corners[i].Position.Y) * 100.0f;
                if ((p[index] - expected).Length() > 0.001f)
                {
                    ++wrongPlace;
                }
                var uv = lod.VertexBuffer.VertexData[index].HalfPrecisionUVs[0];
                double uvError = Math.Max(Math.Abs(uv.X - corners[i].Uv.X), Math.Abs(uv.Y - corners[i].Uv.Y));
                worstUv = Math.Max(worstUv, uvError);
                wrongUv += uvError > 0.002 ? 1 : 0;
            }
            Report(wrongPlace == 0, $"{name}: every corner where the glb says ({wrongPlace} off by more than 0.001 units)");
            Report(wrongUv == 0, $"{name}: every corner's UV as the glb says, within half-float precision (worst {worstUv:F5}, {wrongUv} off by more than 0.002)");
        }
        Console.WriteLine(failures == 0 ? "All checks passed." : $"{failures} CHECKS FAILED.");
        return failures == 0 ? 0 : 1;
    }
}
