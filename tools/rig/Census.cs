// SPDX-License-Identifier: GPL-3.0-only
// mevr-rig census <BioGame folder> <out.csv> [--name regex] [--threads n]: every skeletal mesh whose name matches (by default the
// human female and asari bodies: HMF_ / ASA_ with ARM, the body meshes), in every package of the game and its DLC, with what
// tells copies apart (LOD 0's vertex count and a hash of its positions, the bone names' hash) and whether the skeleton has the
// soft-tissue bones' parents (Chest2, Pelvis). The list is the player's game data's: kept local (%LOCALAPPDATA%\MEVR\rig),
// never committed (docs/trilogy-hair-plan.md section 12, tools/rig/README.md "Every female body").
using System;
using System.Collections.Concurrent;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.Unreal.BinaryConverters;

namespace Mevr.Rig;

internal static class Census
{
    public const string DefaultName = @"^(HMF|ASA)_ARM_";

    public static int Run(string[] args)
    {
        string root = args[1];
        string outPath = args[2];
        var regex = new System.Text.RegularExpressions.Regex(Program.Arg(args, "--name") ?? DefaultName, System.Text.RegularExpressions.RegexOptions.IgnoreCase);
        int threads = int.Parse(Program.Arg(args, "--threads") ?? "4", CultureInfo.InvariantCulture);
        var files = Directory.EnumerateFiles(root, "*.pcc", SearchOption.AllDirectories).ToList();
        var rows = new ConcurrentBag<string>();
        int done = 0;
        Parallel.ForEach(files, new ParallelOptions { MaxDegreeOfParallelism = threads }, file =>
        {
            try
            {
                using var package = MEPackageHandler.OpenMEPackage(file, forceLoadFromDisk: true);
                foreach (var e in package.Exports.Where(e => e.ClassName == "SkeletalMesh" && regex.IsMatch(e.ObjectName.Instanced)))
                {
                    try
                    {
                        var mesh = ObjectBinary.From<SkeletalMesh>(e);
                        var lod = mesh.LODModels[0];
                        var names = mesh.RefSkeleton.Select(b => b.Name.Instanced).ToArray();
                        bool chest2 = names.Any(n => n.Equals("Chest2", StringComparison.OrdinalIgnoreCase));
                        bool pelvis = names.Any(n => n.Equals("Pelvis", StringComparison.OrdinalIgnoreCase));
                        bool soft = names.Any(n => n.StartsWith("Breast", StringComparison.OrdinalIgnoreCase) || n.StartsWith("Glute", StringComparison.OrdinalIgnoreCase));
                        string rel = Path.GetRelativePath(root, file);
                        rows.Add(string.Join(",", rel, e.InstancedFullPath, e.ObjectName.Instanced, lod.NumVertices, names.Length,
                            chest2 ? 1 : 0, pelvis ? 1 : 0, soft ? 1 : 0, Hash(lod), HashNames(names), mesh.LODModels.Length,
                            string.Join("/", lod.Chunks.Select(c => c.BoneMap.Length)), e.IsForcedExport ? 1 : 0));
                    }
                    catch (Exception ex)
                    {
                        rows.Add($"{Path.GetRelativePath(root, file)},{e.InstancedFullPath},{e.ObjectName.Instanced},error,{ex.GetType().Name}");
                    }
                }
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"{Path.GetFileName(file)}: {ex.Message}");
            }
            int n = System.Threading.Interlocked.Increment(ref done);
            if (n % 250 == 0) Console.Error.WriteLine($"  {n} of {files.Count} packages");
        });
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(outPath)));
        File.WriteAllLines(outPath, new[] { "file,path,name,vertices,bones,chest2,pelvis,soft,meshHash,boneHash,lods,chunkBones,forced" }.Concat(rows.OrderBy(r => r, StringComparer.Ordinal)));
        Console.WriteLine($"{rows.Count} meshes in {files.Count} packages -> {outPath}");
        return 0;
    }

    // LOD 0's positions and influences (a cooked copy of the same mesh in another package hashes the same).
    static string Hash(StaticLODModel lod)
    {
        using var sha = SHA1.Create();
        var data = lod.VertexBufferGPUSkin.VertexData;
        var buf = new byte[data.Length * 20];
        for (int i = 0; i < data.Length; ++i)
        {
            BitConverter.TryWriteBytes(buf.AsSpan(i * 20), data[i].Position.X);
            BitConverter.TryWriteBytes(buf.AsSpan(i * 20 + 4), data[i].Position.Y);
            BitConverter.TryWriteBytes(buf.AsSpan(i * 20 + 8), data[i].Position.Z);
            buf[i * 20 + 12] = data[i].InfluenceWeights[0]; buf[i * 20 + 13] = data[i].InfluenceWeights[1];
            buf[i * 20 + 14] = data[i].InfluenceWeights[2]; buf[i * 20 + 15] = data[i].InfluenceWeights[3];
        }
        return Convert.ToHexString(sha.ComputeHash(buf))[..12];
    }

    static string HashNames(string[] names)
    {
        using var sha = SHA1.Create();
        return Convert.ToHexString(sha.ComputeHash(Encoding.UTF8.GetBytes(string.Join("|", names.Select(n => n.ToLowerInvariant())))))[..10];
    }
}
