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
    // TEXTUREGROUP_Environment_1024 (BIOEngine.ini: MaxLODSize 4096), as the game's packages name it: the name
    // "TEXTUREGROUP_Environment" with instance number 1025 (UE3 stores a "_1024" suffix as the number plus one).
    public static readonly NameReference TextureGroup = new("TEXTUREGROUP_Environment", 1025);
    public const string BackingSuffix = "_Backing";
    public const string ScreenMaterial = "Screen";  // a glTF material of this name is the live screen: an instance of its own
    private const int MaxIndexableVertices = 65535;             // the index buffer is 16-bit, and elements have no base vertex
    // A texture size cap per group, below --max-texture: the controls are small, and at 4096 they had 6,800 to 9,300 texels a
    // metre where a hand on them sees about 3,000 (research/48 section 24); 2048 keeps 3,400 to 4,600 and frees 16 MB.
    private static readonly Dictionary<string, int> GroupMaxTexture = new() { ["Joystick"] = 2048, ["Throttle"] = 2048 };

    // The object names, the file names they come from, which texture they share, and where the file is: "detached"
    // (detached/<file>.glb), "repair" (Newest below) or a path under the assets folder. The hull's pieces share its origin
    // (tools/cockpit/hull_assemble.py); the two side panes share one texture. The hull replaces the earlier front cockpit
    // (the user, 2026-11-02), which is no longer packaged.
    private const string Hull = "hull reconstruction/assembled/";
    private static readonly (string File, string Name, string Group, string Source)[] Models =
    {
        ("joystick-base", "Joystick_Base", "Joystick", "detached"),
        ("joystick-grip", "Joystick_Grip", "Joystick", "detached"),
        ("throtle-control-base", "Throttle_Base", "Throttle", "detached"),
        ("throtle-control-handle", "Throttle_Handle", "Throttle", "detached"),
        ("chair", "Chair", "Chair", "repair"),
        ("hull-shell", "Hull_Shell", "Hull_Shell", Hull + "shell.glb"),
        ("hull-console", "Hull_Console", "Hull_Console", Hull + "console.glb"),
        ("hull-side-left", "Hull_Side_Left", "Hull_Side", Hull + "side-left.glb"),
        ("hull-side-right", "Hull_Side_Right", "Hull_Side", Hull + "side-right.glb"),
        ("aim-reticle", "Aim_Reticle", "Aim_Reticle", Hull + "aim-reticle.glb"),  // the game's aiming capture's reticle (research/48 section 24)
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

    // tools/cockpit/game_ready.py's model wins when its manifest says it was made from the file the model's source picks
    // (the source's SHA-256); one made from another file stops the build. raw: the repaired and detached models as they are.
    public static List<ModelSource> Sources(string assets, bool raw = false)
    {
        Dictionary<string, string> ready = raw ? null : GameReady(assets);
        var sources = new List<ModelSource>();
        foreach (var model in Models)
        {
            string path = model.Source switch
            {
                "detached" => System.IO.Path.Combine(assets, "detached", model.File + ".glb"),
                "repair" => Newest(assets, model.File),
                _ => System.IO.Path.Combine(assets, model.Source),
            };
            if (!File.Exists(path))
            {
                throw new FileNotFoundException($"Missing model {model.File}: {path}");
            }
            if (ready != null && ready.TryGetValue(model.File, out string sourceSha))
            {
                if (!string.Equals(sourceSha, Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))), StringComparison.OrdinalIgnoreCase))
                {
                    throw new InvalidDataException($"game-ready/{model.File}.glb was made from another {path}: run tools/cockpit/game_ready.py again, or build with --raw");
                }
                path = System.IO.Path.Combine(assets, "game-ready", model.File + ".glb");
            }
            sources.Add(new ModelSource(model.Name, path, model.Group));
        }
        return sources;
    }

    private static Dictionary<string, string> GameReady(string assets)
    {
        string manifest = System.IO.Path.Combine(assets, "game-ready", "manifest.json");
        if (!File.Exists(manifest))
        {
            return null;
        }
        using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(manifest));
        return doc.RootElement.GetProperty("models").EnumerateObject().ToDictionary(m => m.Name, m => m.Value.GetProperty("sourceSha256").GetString());
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
        ExportEntry export = Texture2D.CreateTexture(pcc, new NameReference(name, 0), width, height, PixelFormat.DXT1, true, null, TextureGroup);
        MakeLoadable(export);
        var texture = new Texture2D(export);
        PropertyCollection props = export.GetProperties();
        props.AddOrReplaceProp(new IntProperty(width, "OriginalSizeX"));
        props.AddOrReplaceProp(new IntProperty(height, "OriginalSizeY"));
        props.AddOrReplaceProp(new IntProperty(0, "MipTailBaseIdx"));
        // The LOD group decides how many mips the game makes resident. Written as the one name "TEXTUREGROUP_Environment_1024"
        // (number 0), the game's enum didn't find it and the texture got index 27, past the enum's last group: RequestedMips 1,
        // only the 4x4 mip, the whole model one flat colour (research/47).
        props.AddOrReplaceProp(new EnumProperty(TextureGroup, "TextureGroup", pcc.Game, "LODGroup"));
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

    private static ExportEntry AddInstance(IMEPackage pcc, string name, IEntry parent, ExportEntry texture, Guid expressionGuid, Guid parentLightingGuid)
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
        // The instance's own and its parent's GUIDs, as every instance of the game's holds them.
        props.AddOrReplaceProp(CommonStructs.GuidProp(parentLightingGuid, "ParentLightingGuid"));
        props.AddOrReplaceProp(CommonStructs.GuidProp(Guid.NewGuid(), "m_Guid"));
        props.AddOrReplaceProp(CommonStructs.GuidProp(Guid.NewGuid(), "LightingGuid"));
        export.WriteProperties(props);
        return export;
    }

    // The parameter's expression GUID in the game's own material; zero (what many of the game's instances hold) if the game isn't here.
    public static Guid ParentExpressionGuid(string gamePath, out string note, out Guid parentLightingGuid)
    {
        parentLightingGuid = Guid.Empty;
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
        parentLightingGuid = material.GetProperties().GetProp<StructProperty>("LightingGuid") is { } l ? CommonStructs.GetGuid(l) : Guid.Empty;
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

        Guid expressionGuid = ParentExpressionGuid(gamePath, out string parentNote, out Guid parentLightingGuid);
        Console.WriteLine($"Parent: {parentNote}");

        using IMEPackage pcc = MEPackageHandler.CreateAndOpenPackage(outPath, MEGame.LE1, forceLoadFromDisk: true);
        IEntry parentPackage = ExportCreator.CreatePackageImport(pcc, ParentPackage);
        IEntry parent = ExportCreator.CreateImport(pcc, "Material", ParentMaterial, parentPackage);

        var report = new List<Dictionary<string, object>>();
        var textures = new Dictionary<string, (ExportEntry Texture, int Width, int Height)>();
        var instances = new Dictionary<string, ExportEntry>();
        var backingImage = LecImage.LoadFromRaw(SolidBgr(4, 4, new Vector3(0.035f, 0.04f, 0.044f)), PixelFormat.RGB, 4, 4);
        ExportEntry backingTexture = AddTexture(pcc, "Backing_Diff", backingImage, 4, 4);
        ExportEntry backingInstance = AddInstance(pcc, "Backing_MIC", parent, backingTexture, expressionGuid, parentLightingGuid);

        bool raw = options.Has("raw");
        foreach (ModelSource source in Sources(assets, raw))
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

            // A texture per image, shared by every model with the same bytes (the two parts of a detached model, the hull's two
            // sides): the model's first image is <Group>_Diff with the instance <Group>_MIC, another <Group>_<material>_Diff and
            // _MIC (the console's idle screens). The live screen (material "Screen") gets an instance of its own,
            // <Group>_Screen_MIC, whose texture the mod replaces with the capture's (research/48).
            int mainImage = welded.Select(p => p.BaseColorImage).FirstOrDefault(i => i.HasValue) ?? throw new InvalidDataException($"{source.Name} has no base colour image");
            (ExportEntry Texture, int Width, int Height) Texture(int imageIndex, string suffix)
            {
                byte[] encoded = model.LogicalImages[imageIndex].Content.Content.ToArray();
                string hash = Convert.ToHexString(SHA256.HashData(encoded));
                if (!textures.TryGetValue(hash, out var made))
                {
                    int cap = Math.Min(maxTexture, GroupMaxTexture.TryGetValue(source.Group, out int groupCap) ? groupCap : maxTexture);
                    LecImage image = Pixels(encoded, cap, out int width, out int height, out Vector3 mean);
                    made = (AddTexture(pcc, source.Group + suffix + "_Diff", image, width, height), width, height);
                    textures.Add(hash, made);
                    Console.WriteLine($"  texture {made.Texture.InstancedFullPath} {width}x{height} DXT1 ({new UTextureInfo(made.Texture).Summary}), source mean RGB {mean.X:F1} {mean.Y:F1} {mean.Z:F1}");
                }
                return made;
            }
            ExportEntry Instance(WeldedPrimitive primitive)
            {
                bool screen = primitive.MaterialName == ScreenMaterial;
                if (!primitive.BaseColorImage.HasValue && !screen)
                {
                    return backingInstance;
                }
                string suffix = primitive.BaseColorImage == mainImage ? "" : "_" + primitive.MaterialName;
                ExportEntry texture = primitive.BaseColorImage.HasValue ? Texture(primitive.BaseColorImage.Value, suffix).Texture : backingTexture;
                string name = source.Group + (screen ? "_" + ScreenMaterial : suffix) + "_MIC";
                if (!instances.TryGetValue(name, out ExportEntry instance))
                {
                    instance = AddInstance(pcc, name, parent, texture, expressionGuid, parentLightingGuid);
                    instances.Add(name, instance);
                }
                return instance;
            }
            var main = Texture(mainImage, "");

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
                    lod.Elements[i].Material = Instance(primitives[i]).UIndex;
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
                    ["texture"] = main.Texture.ObjectName.Instanced,
                    ["textureSize"] = new[] { main.Width, main.Height },
                });
            }
        }
        ApplyCookedFlags(pcc);
        pcc.Save();
        pcc.Dispose();
        FixMipOffsets(outPath);

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
        return Check.Run(outPath, assets, gamePath, raw);
    }

    // Each mip's bulk data says where it sits in the uncompressed package. LEC writes it from the export's place in the file at
    // the time it serializes, which is 0 for an export that was never saved: every offset came out relative to the export, the
    // game's own are absolute (research/47). So: saved once (each export has its place), written again, saved. The sizes don't
    // change, so the places hold; Check.cs fails the build otherwise.
    private static void FixMipOffsets(string path)
    {
        using IMEPackage pcc = MEPackageHandler.OpenMEPackage(path, forceLoadFromDisk: true);
        foreach (ExportEntry export in pcc.Exports.Where(e => e.ClassName == "Texture2D"))
        {
            export.WriteBinary(ObjectBinary.From<UTexture2D>(export));
        }
        pcc.Save();
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
