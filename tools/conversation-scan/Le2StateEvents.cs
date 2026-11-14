// SPDX-License-Identifier: MIT
// Inspect selected LE2 packages through LegendaryExplorer's binary converter.
// No mounted-file precedence or transitive story/progression completeness is inferred.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.Json;
using LegendaryExplorerCore.Unreal.BinaryConverters;

namespace Mevr.ConversationScan;

internal static class Le2StateEvents
{
    internal static int Run(string cooked, string outPath, string idList, string only)
    {
        var ids = new SortedSet<int>();
        foreach (string token in idList.Split(','))
        {
            if (!int.TryParse(token, NumberStyles.None, CultureInfo.InvariantCulture, out int id))
            {
                Console.Error.WriteLine("LE2 --state-events requires comma-separated nonnegative integer IDs.");
                return 2;
            }
            ids.Add(id);
        }
        if (string.IsNullOrWhiteSpace(only) || only.StartsWith("--", StringComparison.Ordinal))
        {
            Console.Error.WriteLine("LE2 --state-events requires an explicit --only package-name selector (for example Startup_INT).");
            return 2;
        }
        InspectionContext.Initialize();
        string[] files = Directory.GetFiles(cooked, "*.pcc")
            .Where(file => Path.GetFileName(file).Contains(only, StringComparison.OrdinalIgnoreCase))
            .OrderBy(file => file, StringComparer.OrdinalIgnoreCase).ToArray();
        var packages = new List<object>();
        var events = new List<object>();
        var errors = new List<object>();
        var matches = ids.ToDictionary(id => id, _ => 0);
        var elementOptions = new JsonSerializerOptions { IncludeFields = true };
        if (files.Length == 0)
            errors.Add(new { packageFile = (string)null, error = "No packages match the supplied selector in this directory." });
        foreach (string file in files)
        {
            string path = Path.GetFullPath(file);
            string hash = null;
            try
            {
                using (var bytes = File.OpenRead(path))
                    hash = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
                using var package = InspectionContext.Open(path);
                var maps = package.Exports.Where(e => e.ClassName == "BioStateEventMap").ToList();
                packages.Add(new
                {
                    packageFile = path, packageSha256 = hash, game = package.Game.ToString(),
                    maps = maps.Select(map => new { path = map.InstancedFullPath, exportIndex = map.UIndex }).ToArray(),
                });
                foreach (var mapExport in maps)
                {
                    try
                    {
                        var map = ObjectBinary.From<BioStateEventMap>(mapExport);
                        foreach (var stateEvent in map.StateEvents.Where(e => ids.Contains(e.ID)))
                        {
                            // Serialize each concrete element including nested data. Primitive-only summaries can
                            // otherwise hide substate/function parameters and make dependency claims misleading.
                            var elements = stateEvent.Elements.Select(element => new
                            {
                                type = element.GetType().Name,
                                data = JsonSerializer.SerializeToElement(element, element.GetType(), elementOptions),
                            }).ToArray();
                            events.Add(new
                            {
                                id = stateEvent.ID, packageFile = path, packageSha256 = hash,
                                mapPath = mapExport.InstancedFullPath, mapExportIndex = mapExport.UIndex, elements,
                            });
                            matches[stateEvent.ID]++;
                        }
                    }
                    catch (Exception e)
                    {
                        errors.Add(new { packageFile = path, packageSha256 = hash,
                            mapPath = mapExport.InstancedFullPath, error = e.GetType().Name + ": " + e.Message });
                    }
                }
            }
            catch (Exception e)
            {
                errors.Add(new { packageFile = path, packageSha256 = hash, error = e.GetType().Name + ": " + e.Message });
            }
        }
        var missing = matches.Where(pair => pair.Value == 0).Select(pair => pair.Key).ToArray();
        bool complete = errors.Count == 0 && missing.Length == 0;
        var report = new
        {
            schemaVersion = 1, game = "LE2",
            scope = new { sourceDirectory = Path.GetFullPath(cooked), packageSelector = only,
                enumeration = "top-level .pcc files in supplied directory only", mountedCompleteness = false,
                limitations = "Direct serialized event elements only; no runtime, transitive-effect or progression acceptance." },
            requestedIds = ids.ToArray(), packages, events, missingIds = missing,
            duplicateIds = matches.Where(pair => pair.Value > 1).Select(pair => pair.Key).ToArray(),
            errors, scopedInspectionComplete = complete,
        };
        using (var writer = new StreamWriter(new FileStream(outPath, FileMode.CreateNew, FileAccess.Write)))
            writer.WriteLine(JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine($"{packages.Count} packages inspected, {events.Count} matching events, {missing.Length} missing IDs, {errors.Count} errors -> {outPath}");
        return complete ? 0 : 1;
    }
}
