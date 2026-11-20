// SPDX-License-Identifier: GPL-3.0-only
// mevr-rig contents <package.pcc>: every export (class, path, size) and a skeletal mesh's properties, to check a written
// package before the game loads it.
using System;
using System.Linq;
using LegendaryExplorerCore.Packages;

namespace Mevr.Rig;

internal static class Contents
{
    public static int Run(string[] args)
    {
        using var package = MEPackageHandler.OpenMEPackage(args[1], forceLoadFromDisk: true);
        Console.WriteLine($"{package.FilePath}: {package.ExportCount} exports, {package.ImportCount} imports, {package.NameCount} names, flags {package.Flags}");
        foreach (var e in package.Exports)
        {
            Console.WriteLine($"  {e.ClassName,-28} {e.InstancedFullPath} ({e.DataSize} bytes{(e.IsForcedExport ? ", forced" : "")})");
        }
        foreach (var i in package.Imports)
        {
            Console.WriteLine($"  import {i.ClassName,-21} {i.InstancedFullPath}");
        }
        foreach (var e in package.Exports.Where(e => e.ClassName == "SkeletalMesh"))
        {
            Console.WriteLine($"  {e.ObjectName.Instanced} properties:");
            foreach (var p in e.GetProperties())
            {
                string value = p is LegendaryExplorerCore.Unreal.ArrayPropertyBase a ? $"array of {a.Count}" : p.ToString();
                Console.WriteLine($"    {p.Name.Instanced}: {value}");
            }
        }
        return 0;
    }

    // mevr-rig find <folder> <file pattern> <mesh name regex>: every SkeletalMesh whose name matches, per package.
    public static int Find(string[] args)
    {
        var regex = new System.Text.RegularExpressions.Regex(args[3], System.Text.RegularExpressions.RegexOptions.IgnoreCase);
        foreach (var file in System.IO.Directory.EnumerateFiles(args[1], args[2], System.IO.SearchOption.AllDirectories))
        {
            try
            {
                using var package = MEPackageHandler.OpenMEPackage(file, forceLoadFromDisk: true);
                foreach (var e in package.Exports.Where(e => e.ClassName == "SkeletalMesh" && regex.IsMatch(e.ObjectName.Instanced)))
                {
                    Console.WriteLine($"{System.IO.Path.GetFileName(file)}: {e.InstancedFullPath}");
                }
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"{System.IO.Path.GetFileName(file)}: {ex.Message}");
            }
        }
        return 0;
    }
}
