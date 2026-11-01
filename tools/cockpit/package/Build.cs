// SPDX-License-Identifier: GPL-3.0-only
// Models -> package: welds each .glb, runs Legendary Explorer's glTF importer, adds textures and material instances.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;
using LegendaryExplorerCore.Helpers;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.Textures;
using LegendaryExplorerCore.Unreal;
using LegendaryExplorerCore.Unreal.BinaryConverters;
using LegendaryExplorerCore.Unreal.Classes;
using SharpGLTF.Schema2;
using SixLabors.ImageSharp;
using SixLabors.ImageSharp.PixelFormats;
using SixLabors.ImageSharp.Processing;
using LecImage = LegendaryExplorerCore.Textures.Image;
using PixelFormat = LegendaryExplorerCore.Textures.PixelFormat;
using Texture2D = LegendaryExplorerCore.Unreal.Classes.Texture2D;

namespace Mevr.CockpitPackage;

internal sealed record ModelSource(string Name, string Path, string Group);

// One glTF primitive, welded: the vertices it really has, the corners as the file lists them.
internal sealed class WeldedPrimitive
{
    public string MaterialName;
    public int? BaseColorImage;
    public Vector4 BaseColorFactor = Vector4.One;
    public List<Vector3> Positions = new();
    public List<Vector3> Normals = new();
    public List<Vector2> Uvs = new();
    public List<int> Indices = new();
    public int SourceVertices;
}

internal static class Build
{
    public const string ParentPackage = "EngineMaterials";
    public const string ParentMaterial = "EmissiveTexturedMaterial";
    public const string ParameterName = "Texture";
    public const string TextureGroup = "TEXTUREGROUP_Vehicle";  // BIOEngine.ini: MaxLODSize 4096
    public const string BackingSuffix = "_Backing";
    private const int MaxIndexableVertices = 65535;             // the index buffer is 16-bit, and elements have no base vertex

    // The object names, the file names they come from, which texture they share.
    private static readonly (string File, string Name, string Group, bool Detached)[] Models =
    {
        ("joystick-base", "Joystick_Base", "Joystick", true),
        ("joystick-grip", "Joystick_Grip", "Joystick", true),
        ("throtle-control-base", "Throttle_Base", "Throttle", true),
        ("throtle-control-handle", "Throttle_Handle", "Throttle", true),
        ("chair", "Chair", "Chair", false),
        ("front-cockpit", "Front_Cockpit", "Front_Cockpit", false),
    };

    // The rule of tools/cockpit/detach_controls.py: the highest vNNN under repairs/<date>/<name>/, else the file beside repairs/.
    public static string Newest(string assets, string name)
    {
        var found = new List<(int Version, string Path)>();
        string repairs = System.IO.Path.Combine(assets, "repairs");
        if (Directory.Exists(repairs))
        {
            foreach (string file in Directory.EnumerateFiles(repairs, "*.glb", SearchOption.AllDirectories))
            {
                string fileName = System.IO.Path.GetFileName(file);
                if (fileName != "Repaired.glb" && fileName != "Backed_Source.glb")
                {
                    continue;
                }
                string[] parts = System.IO.Path.GetDirectoryName(file).Replace('\\', '/').Split('/');
                if (!parts.TakeLast(2).Contains(name))
                {
                    continue;
                }
                Match version = Regex.Match(parts[^1], @"^v(\d+)$");
                found.Add((version.Success ? int.Parse(version.Groups[1].Value) : 1, file));
            }
        }
        return found.Count > 0 ? found.OrderBy(f => f.Version).ThenBy(f => f.Path, StringComparer.Ordinal).Last().Path : System.IO.Path.Combine(assets, name + ".glb");
    }

    public static List<ModelSource> Sources(string assets)
    {
        var sources = new List<ModelSource>();
        foreach (var model in Models)
        {
            string path = model.Detached ? System.IO.Path.Combine(assets, "detached", model.File + ".glb") : Newest(assets, model.File);
            if (!File.Exists(path))
            {
                throw new FileNotFoundException($"Missing model {model.File}: {path}");
            }
            sources.Add(new ModelSource(model.Name, path, model.Group));
        }
        return sources;
    }

    // Exact duplicates only (the same position, normal and UV bits): nothing moves, the corners keep their order.
    public static List<WeldedPrimitive> Weld(ModelRoot model)
    {
        var welded = new List<WeldedPrimitive>();
        foreach (Node node in model.LogicalNodes.Where(n => n.Mesh != null))
        {
            foreach (MeshPrimitive primitive in node.Mesh.Primitives)
            {
                var positions = primitive.GetVertexAccessor("POSITION").AsVector3Array();
                var normals = primitive.GetVertexAccessor("NORMAL")?.AsVector3Array();
                var uvs = primitive.GetVertexAccessor("TEXCOORD_0")?.AsVector2Array();
                if (normals == null || uvs == null)
                {
                    throw new InvalidDataException($"A primitive of {node.Name} has no normals or no UVs");
                }
                var result = new WeldedPrimitive { MaterialName = primitive.Material?.Name ?? "null", SourceVertices = positions.Count };
                MaterialChannel? channel = primitive.Material?.FindChannel("BaseColor");
                if (channel is MaterialChannel baseColor)
                {
                    result.BaseColorFactor = baseColor.Color;
                    result.BaseColorImage = baseColor.Texture?.PrimaryImage?.LogicalIndex;
                }
                var seen = new Dictionary<(Vector3, Vector3, Vector2), int>(positions.Count);
                foreach ((int a, int b, int c) in primitive.GetTriangleIndices())
                {
                    foreach (int corner in new[] { a, b, c })
                    {
                        var key = (positions[corner], normals[corner], uvs[corner]);
                        if (!seen.TryGetValue(key, out int index))
                        {
                            index = result.Positions.Count;
                            seen.Add(key, index);
                            result.Positions.Add(key.Item1);
                            result.Normals.Add(key.Item2);
                            result.Uvs.Add(key.Item3);
                        }
                        result.Indices.Add(index);
                    }
                }
                welded.Add(result);
            }
        }
        return welded;
    }

    private static ModelRoot Rebuild(string name, List<WeldedPrimitive> primitives)
    {
        ModelRoot model = ModelRoot.CreateModel();
        Mesh mesh = model.CreateMesh(name);
        for (int i = 0; i < primitives.Count; ++i)
        {
            WeldedPrimitive p = primitives[i];
            MeshPrimitive primitive = mesh.CreatePrimitive();
            primitive.WithVertexAccessor("POSITION", p.Positions);
            primitive.WithVertexAccessor("NORMAL", p.Normals);
            primitive.WithVertexAccessor("TEXCOORD_0", p.Uvs);
            primitive.WithIndicesAccessor(PrimitiveType.TRIANGLES, p.Indices);
            primitive.Material = model.CreateMaterial($"MevrSlot{i}");
        }
        model.UseScene("Scene").CreateNode(name).Mesh = mesh;
        return model;
    }

    // The base colour's pixels, at a power-of-two size no larger than maxSize, as B G R bytes (what LEC's Image calls RGB; no alpha, so DXT1 keeps its four-colour blocks).
    private static LecImage Pixels(byte[] encoded, int maxSize, out int width, out int height, out Vector3 meanRgb)
    {
        using Image<Bgr24> image = SixLabors.ImageSharp.Image.Load<Bgr24>(encoded);
        width = FloorPow2(Math.Min(image.Width, maxSize));
        height = FloorPow2(Math.Min(image.Height, maxSize));
        if (width != image.Width || height != image.Height)
        {
            int w = width;
            int h = height;
            image.Mutate(x => x.Resize(w, h, KnownResamplers.Lanczos3));
        }
        byte[] raw = new byte[width * height * 3];
        image.CopyPixelDataTo(raw);
        meanRgb = MeanRgb(raw, 3);
        return LecImage.LoadFromRaw(raw, PixelFormat.RGB, width, height);
    }

    public static Vector3 MeanRgb(byte[] bgr, int stride)
    {
        double r = 0, g = 0, b = 0;
        long pixels = bgr.Length / stride;
        for (long i = 0; i < pixels; ++i)
        {
            b += bgr[i * stride];
            g += bgr[i * stride + 1];
            r += bgr[i * stride + 2];
        }
        return new Vector3((float)(r / pixels), (float)(g / pixels), (float)(b / pixels));
    }

    private static int FloorPow2(int value) => 1 << (31 - BitOperations.LeadingZeroCount((uint)value));

    private static void MakeLoadable(ExportEntry export)
    {
        // As the game's own objects: found by name from other packages, kept by the garbage collection while the package stays.
        export.ObjectFlags = UnrealFlags.EObjectFlags.Public | UnrealFlags.EObjectFlags.LoadForClient | UnrealFlags.EObjectFlags.LoadForServer
            | UnrealFlags.EObjectFlags.LoadForEdit | UnrealFlags.EObjectFlags.Standalone;
    }

    private static ExportEntry AddTexture(IMEPackage pcc, string name, LecImage image, int width, int height)
    {
        ExportEntry export = Texture2D.CreateTexture(pcc, new NameReference(name, 0), width, height, PixelFormat.DXT1, true, null, new NameReference(TextureGroup, 0));
        MakeLoadable(export);
        var texture = new Texture2D(export);
        PropertyCollection props = export.GetProperties();
        props.AddOrReplaceProp(new IntProperty(width, "OriginalSizeX"));
        props.AddOrReplaceProp(new IntProperty(height, "OriginalSizeY"));
        props.AddOrReplaceProp(new IntProperty(0, "MipTailBaseIdx"));
        foreach (string message in texture.Replace(image, props, isPackageStored: true, forcedNewFormat: PixelFormat.DXT1, forceMipping: true))
        {
            Console.WriteLine($"  {name}: {message}");
        }
        // Each its own GUID, as the game's. (The last mips read 4x4, 4x4, 4x4: block sizes, as the game's DXT textures hold them too.)
        var binary = ObjectBinary.From<UTexture2D>(export);
        binary.TextureGuid = Guid.NewGuid();
        export.WriteBinary(binary);
        return export;
    }

    private static ExportEntry AddInstance(IMEPackage pcc, string name, IEntry parent, ExportEntry texture, Guid expressionGuid)
    {
        ExportEntry export = ExportCreator.CreateExport(pcc, new NameReference(name, 0), "MaterialInstanceConstant", indexed: false);
        MakeLoadable(export);
        PropertyCollection props = export.GetProperties();
        props.AddOrReplaceProp(new ObjectProperty(parent.UIndex, "Parent"));
        var value = new PropertyCollection
        {
            CommonStructs.GuidProp(expressionGuid, "ExpressionGUID"),
            new NameProperty(ParameterName, "ParameterName"),
            new ObjectProperty(texture.UIndex, "ParameterValue"),
        };
        props.AddOrReplaceProp(new ArrayProperty<StructProperty>(new[] { new StructProperty("TextureParameterValue", value) }, "TextureParameterValues"));
        props.AddOrReplaceProp(CommonStructs.GuidProp(Guid.NewGuid(), "LightingGuid"));
        export.WriteProperties(props);
        return export;
    }

    // The parameter's expression GUID in the game's own material; zero (what many of the game's instances hold) if the game isn't here.
    public static Guid ParentExpressionGuid(string gamePath, out string note)
    {
        string startup = System.IO.Path.Combine(gamePath, "BioGame", "CookedPCConsole", "Startup_INT.pcc");
        note = "the game's Startup_INT.pcc wasn't read: the instance's ExpressionGUID is zero";
        if (!File.Exists(startup))
        {
            return Guid.Empty;
        }
        using IMEPackage pcc = MEPackageHandler.OpenMEPackage(startup, forceLoadFromDisk: true);
        ExportEntry material = pcc.FindExport($"{ParentPackage}.{ParentMaterial}");
        ExportEntry expression = pcc.Exports.FirstOrDefault(e => e.Parent == material && e.ClassName.StartsWith("MaterialExpressionTextureSampleParameter", StringComparison.Ordinal));
        if (expression == null)
        {
            note = "no texture parameter expression under the parent in Startup_INT.pcc";
            return Guid.Empty;
        }
        PropertyCollection props = expression.GetProperties();
        string parameter = props.GetProp<NameProperty>("ParameterName")?.Value.Instanced;
        if (!string.Equals(parameter, ParameterName, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException($"{ParentMaterial}'s parameter is '{parameter}', not '{ParameterName}'");
        }
        Guid guid = props.GetProp<StructProperty>("ExpressionGUID") is { } g ? CommonStructs.GetGuid(g) : Guid.Empty;
        note = $"{material.InstancedFullPath} in Startup_INT.pcc: parameter {parameter}, expression GUID {guid}; " + string.Join(", ", material.GetProperties().Where(p => p is BoolProperty or EnumProperty).Select(Verbs.Describe));
        return guid;
    }

    public static int Run(Options options, string gamePath)
    {
        string assets = options.Get("assets");
        if (assets == null || !Directory.Exists(assets))
        {
            Console.Error.WriteLine($"--assets: no folder '{assets}'");
            return 2;
        }
        string repo = options.Get("repo") ?? FindRepoRoot();
        string outPath = System.IO.Path.GetFullPath(options.Get("out") ?? System.IO.Path.Combine(repo, "build", "cockpit", "BIOG_MEVR_Cockpit.pcc"));
        int maxTexture = int.Parse(options.Get("max-texture") ?? "4096");
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(outPath));
        File.Delete(outPath);
        string packageName = System.IO.Path.GetFileNameWithoutExtension(outPath);

        Guid expressionGuid = ParentExpressionGuid(gamePath, out string parentNote);
        Console.WriteLine($"Parent: {parentNote}");

        using IMEPackage pcc = MEPackageHandler.CreateAndOpenPackage(outPath, MEGame.LE1, forceLoadFromDisk: true);
        IEntry parentPackage = ExportCreator.CreatePackageImport(pcc, ParentPackage);
        IEntry parent = ExportCreator.CreateImport(pcc, "Material", ParentMaterial, parentPackage);

        var report = new List<Dictionary<string, object>>();
        var textures = new Dictionary<string, (ExportEntry Texture, ExportEntry Instance, int Width, int Height)>();
        var backingImage = LecImage.LoadFromRaw(SolidBgr(4, 4, new Vector3(0.035f, 0.04f, 0.044f)), PixelFormat.RGB, 4, 4);
        ExportEntry backingTexture = AddTexture(pcc, "Backing_Diff", backingImage, 4, 4);
        ExportEntry backingInstance = AddInstance(pcc, "Backing_MIC", parent, backingTexture, expressionGuid);

        foreach (ModelSource source in Sources(assets))
        {
            ModelRoot model = ModelRoot.Load(source.Path, SharpGLTF.Validation.ValidationMode.Skip);
            List<WeldedPrimitive> welded = Weld(model);
            int weldedVertices = welded.Sum(p => p.Positions.Count);
            Console.WriteLine($"{source.Name}: {source.Path}");
            Console.WriteLine($"  {welded.Sum(p => p.SourceVertices)} vertices in the file, {weldedVertices} welded, {welded.Sum(p => p.Indices.Count) / 3} triangles, {welded.Count} primitives");
            // One mesh, unless the 16-bit indices (elements have no base vertex) can't reach all of it: then the backing shell is a mesh of its own.
            var parts = new List<(string Name, List<WeldedPrimitive> Primitives)>();
            if (weldedVertices <= MaxIndexableVertices)
            {
                parts.Add((source.Name, welded));
            }
            else
            {
                parts.Add((source.Name, welded.Where(p => p.BaseColorImage.HasValue).ToList()));
                parts.Add((source.Name + BackingSuffix, welded.Where(p => !p.BaseColorImage.HasValue).ToList()));
                Console.WriteLine($"  over {MaxIndexableVertices} vertices: split into {string.Join(" and ", parts.Select(q => $"{q.Name} ({q.Primitives.Sum(p => p.Positions.Count)})"))}");
            }

            // The instance of the first primitive with an image; images are shared between the two parts of a detached model.
            int imageIndex = welded.Select(p => p.BaseColorImage).FirstOrDefault(i => i.HasValue) ?? throw new InvalidDataException($"{source.Name} has no base colour image");
            byte[] encoded = model.LogicalImages[imageIndex].Content.Content.ToArray();
            string hash = Convert.ToHexString(SHA256.HashData(encoded));
            if (!textures.TryGetValue(hash, out var made))
            {
                LecImage image = Pixels(encoded, maxTexture, out int width, out int height, out Vector3 mean);
                ExportEntry texture = AddTexture(pcc, source.Group + "_Diff", image, width, height);
                ExportEntry instance = AddInstance(pcc, source.Group + "_MIC", parent, texture, expressionGuid);
                made = (texture, instance, width, height);
                textures.Add(hash, made);
                Console.WriteLine($"  texture {texture.InstancedFullPath} {width}x{height} DXT1 ({new UTextureInfo(texture).Summary}), source mean RGB {mean.X:F1} {mean.Y:F1} {mean.Z:F1}");
            }

            foreach ((string partName, List<WeldedPrimitive> primitives) in parts)
            {
                ModelRoot prepared = Rebuild(partName, primitives);
                int before = pcc.ExportCount;
                GLTF.ConvertGltfToMesh(prepared, pcc);
                ExportEntry mesh = pcc.Exports.Skip(before).Single(e => e.ClassName == "StaticMesh");
                mesh.ObjectName = new NameReference(partName, 0);
                MakeLoadable(mesh);

                StaticMesh binary = ObjectBinary.From<StaticMesh>(mesh);
                StaticMeshRenderData lod = binary.LODModels[0];
                // LEC casts indices to 16 bits without looking, and its tangents can add vertices after our count.
                if (lod.NumVertices > MaxIndexableVertices + 1)
                {
                    throw new InvalidDataException($"{partName} has {lod.NumVertices} vertices after LEC's tangents: its indices wrapped.");
                }
                for (int i = 0; i < lod.Elements.Length; ++i)
                {
                    lod.Elements[i].Material = (primitives[i].BaseColorImage.HasValue ? made.Instance : backingInstance).UIndex;
                    // No collision: the actor's is off, and there's no body setup.
                    lod.Elements[i].EnableCollision = false;
                    lod.Elements[i].OldEnableCollision = false;
                }
                binary.unk1 = (uint)binary.LODModels.Length;  // every mesh of the game's holds its LOD count here; LEC leaves 0
                mesh.WriteBinary(binary);

                report.Add(new Dictionary<string, object>
                {
                    ["name"] = $"{packageName}.{partName}",
                    ["source"] = System.IO.Path.GetRelativePath(assets, source.Path).Replace('\\', '/'),
                    ["triangles"] = primitives.Sum(p => p.Indices.Count) / 3,
                    ["fileVertices"] = primitives.Sum(p => p.SourceVertices),
                    ["weldedVertices"] = primitives.Sum(p => p.Positions.Count),
                    ["meshVertices"] = (int)lod.NumVertices,
                    ["materialSlots"] = lod.Elements.Select(e => pcc.GetEntry(e.Material).ObjectName.Instanced).ToArray(),
                    ["texture"] = made.Texture.ObjectName.Instanced,
                    ["textureSize"] = new[] { made.Width, made.Height },
                });
            }
        }
        ApplyCookedFlags(pcc);
        pcc.Save();
        pcc.Dispose();

        var listing = new Dictionary<string, object>
        {
            ["package"] = packageName,
            ["file"] = System.IO.Path.GetFileName(outPath),
            ["bytes"] = new FileInfo(outPath).Length,
            ["objectPrefix"] = packageName + ".",
            ["importerScale"] = "1 glTF unit = 100 Unreal units; axes (x, y, z) -> (x, z, y)",
            ["parentMaterial"] = $"{ParentPackage}.{ParentMaterial}",
            ["meshes"] = report,
        };
        string json = outPath + ".json";
        File.WriteAllText(json, JsonSerializer.Serialize(listing, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine($"Wrote {outPath} ({new FileInfo(outPath).Length / 1048576.0:F1} MB) and {json}");
        return Check.Run(outPath, assets, gamePath);
    }

    // The flags of the game's own cooked packages (research/47): without Cooked a seek-free engine treats it as a source package.
    private static void ApplyCookedFlags(IMEPackage pcc)
    {
        pcc.setFlags(pcc.Flags | UnrealFlags.EPackageFlags.AllowDownload | UnrealFlags.EPackageFlags.Cooked
            | UnrealFlags.EPackageFlags.DisallowLazyLoading | UnrealFlags.EPackageFlags.RequireImportsAlreadyLoaded);
    }

    private static byte[] SolidBgr(int width, int height, Vector3 linear)
    {
        static byte Encode(float value)
        {
            double s = value <= 0.0031308 ? value * 12.92 : 1.055 * Math.Pow(value, 1.0 / 2.4) - 0.055;
            return (byte)Math.Clamp(Math.Round(s * 255.0), 0, 255);
        }
        byte[] raw = new byte[width * height * 3];
        for (int i = 0; i < width * height; ++i)
        {
            raw[i * 3] = Encode(linear.Z);
            raw[i * 3 + 1] = Encode(linear.Y);
            raw[i * 3 + 2] = Encode(linear.X);
        }
        return raw;
    }

    private static string FindRepoRoot()
    {
        string folder = AppContext.BaseDirectory;
        while (folder != null && !File.Exists(System.IO.Path.Combine(folder, "CMakeLists.txt")))
        {
            folder = System.IO.Path.GetDirectoryName(folder);
        }
        return folder ?? Directory.GetCurrentDirectory();
    }

    private readonly struct UTextureInfo
    {
        public readonly string Summary;
        public UTextureInfo(ExportEntry texture)
        {
            var binary = ObjectBinary.From<UTexture2D>(texture);
            Summary = $"{binary.Mips.Count} mips, {binary.Mips.Sum(m => m.CompressedSize) / 1048576.0:F1} MB";
        }
    }
}
