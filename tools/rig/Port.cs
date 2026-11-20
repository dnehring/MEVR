// SPDX-License-Identifier: GPL-3.0-only
// The mesh into a package of ours, alone (tools/rig/README.md "The package"): at the package's root under a new name (the
// path the mod loads: <package>.<name>), its sockets copied as its children, its materials IMPORTS by the original's full
// path. The mod only swaps the rigged mesh onto a component wearing the original, so the original's materials are loaded
// then, and a cooked LE package resolves its imports against objects already in memory (RequireImportsAlreadyLoaded).
// Copying them instead brought 29 master materials and 400 expressions whose paths the game already has loaded.
using System;
using System.Collections.Generic;
using System.Linq;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.Packages.CloningImportingAndRelinking;
using LegendaryExplorerCore.Unreal;
using LegendaryExplorerCore.Unreal.BinaryConverters;

namespace Mevr.Rig;

internal static class Port
{
    private static RelinkerOptionsPackage Rop() => new() { ImportExportDependencies = false };

    public static ExportEntry Mesh(ExportEntry source, IMEPackage pcc, string name, Report report)
    {
        var rop = Rop();
        EntryImporter.ImportAndRelinkEntries(EntryImporter.PortingOption.AddSingularAsChild, source, pcc, null, true, rop, out IEntry entry);
        var mesh = (ExportEntry)entry;
        mesh.ObjectName = NameReference.FromInstancedString(name);
        mesh.ExportFlags &= ~UnrealFlags.EExportFlags.ForcedExport;  // at the root, a forced export would be a top-level object of its own
        // Sockets: the mesh's children in the source, copied as children of ours.
        var sockets = mesh.GetProperty<ArrayProperty<ObjectProperty>>("Sockets");
        var sourceSockets = source.GetProperty<ArrayProperty<ObjectProperty>>("Sockets");
        int copied = 0;
        if (sockets != null && sourceSockets != null)
        {
            for (int i = 0; i < sourceSockets.Count; ++i)
            {
                var s = source.FileRef.GetEntry(sourceSockets[i].Value) as ExportEntry;
                if (s == null)
                {
                    sockets[i].Value = 0;
                    continue;
                }
                EntryImporter.ImportAndRelinkEntries(EntryImporter.PortingOption.AddSingularAsChild, s, pcc, mesh, true, Rop(), out IEntry ns);
                ((ExportEntry)ns).ExportFlags &= ~UnrealFlags.EExportFlags.ForcedExport;
                sockets[i].Value = ns.UIndex;
                ++copied;
            }
            mesh.WriteProperty(sockets);
        }
        foreach (var r in rop.RelinkReport)
        {
            report.Line($"port: {r.Message}");
        }
        report.Line($"port: {mesh.InstancedFullPath}, {copied} socket(s) copied");
        return mesh;
    }

    // A material copied with what it needs, as cooked level packages carry their shared assets (forced exports at their
    // original paths: an object already loaded is used, else ours loads). For a mesh whose materials aren't loaded where
    // the mod puts it on (ME3's underwear body on the Citadel).
    public static void CopyMaterials(ExportEntry source, IMEPackage pcc, SkeletalMesh mesh, Report report)
    {
        var original = ObjectBinary.From<SkeletalMesh>(source);
        for (int i = 0; i < original.Materials.Length; ++i)
        {
            if (source.FileRef.GetEntry(original.Materials[i]) is not ExportEntry m)
            {
                mesh.Materials[i] = 0;
                continue;
            }
            var issues = EntryExporter.ExportExportToPackage(m, pcc, out IEntry copied);
            mesh.Materials[i] = copied.UIndex;
            report.Line($"material {i}: copied {copied.InstancedFullPath} ({issues.Count} porting note(s); the package now has {pcc.ExportCount} exports)");
        }
    }

    // Each material slot an import with the original's full path (its outer packages as package imports).
    public static void Materials(ExportEntry source, IMEPackage pcc, SkeletalMesh mesh, Report report)
    {
        var original = ObjectBinary.From<SkeletalMesh>(source);
        for (int i = 0; i < original.Materials.Length; ++i)
        {
            var m = source.FileRef.GetEntry(original.Materials[i]);
            if (m == null)
            {
                mesh.Materials[i] = 0;
                continue;
            }
            var chain = new List<IEntry>();
            for (var e = m; e != null; e = e.Parent)
            {
                chain.Insert(0, e);
            }
            // An ordinary (not forced) export's outermost package is its file: BIOG_HMF_HIR_PRO.pcc's Hair_PROMiranda.X is
            // BIOG_HMF_HIR_PRO.Hair_PROMiranda.X in memory. A forced export (a level package's copy) names its own.
            IEntry parent = null;
            if (chain[0] is ExportEntry top && !top.IsForcedExport)
            {
                parent = pcc.CreatePackageImport(System.IO.Path.GetFileNameWithoutExtension(source.FileRef.FilePath));
            }
            foreach (var e in chain.Take(chain.Count - 1))
            {
                parent = pcc.CreateImport(e.ClassName, e.ObjectName, parent);
            }
            var import = pcc.CreateImport(m.ClassName, m.ObjectName, parent);
            mesh.Materials[i] = import.UIndex;
            report.Line($"material {i}: import {import.InstancedFullPath} ({m.ClassName})");
        }
    }
}
