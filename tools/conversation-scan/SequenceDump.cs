// SPDX-License-Identifier: GPL-3.0-only
// What a level's script does around a scene (research/69): `conversation-scan <CookedPCConsole> <out.txt> --sequence
// BIOA_STA70_02captain_DSG:SarenCouncil` prints every object of the sequences whose path holds that text: its class, name,
// comment, a few telling properties and where each output leads. So the steps between a line and a conversation's start (a
// teleport, a fade, a matinee, a streaming state) can be read without the game.
// Reads only. Its output is game data: keep it out of the repository.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using LegendaryExplorerCore;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.Unreal;

namespace Mevr.ConversationScan;

internal static class SequenceDump
{
    public static int Run(string cooked, string outPath, string what)
    {
        InspectionContext.Initialize();
        string[] parts = what.Split(':', 2);
        string file = Path.Combine(cooked, parts[0] + ".pcc");
        string text = parts.Length > 1 ? parts[1] : "";
        using var writer = new StreamWriter(outPath);
        using IMEPackage pcc = InspectionContext.Open(file);
        int count = 0;
        foreach (ExportEntry export in pcc.Exports.Where(e =>
            (InspectionContext.IsLe2 ? Le2Inspection.SequencePath(e.InstancedFullPath, true) : e.InstancedFullPath.Contains("Main_Sequence"))
            && e.InstancedFullPath.Contains(text, StringComparison.OrdinalIgnoreCase)))
        {
            PropertyCollection props;
            try
            {
                props = export.GetProperties();
            }
            catch
            {
                continue;
            }
            var facts = new List<string>();
            foreach (Property p in props)
            {
                string name = p.Name.Instanced;
                if (name is "ParentSequence" or "OutputLinks" or "VariableLinks" or "InputLinks" or "EventLinks" or "ObjPosX" or "ObjPosY" or "DrawWidth" or "DrawHeight" or "ObjInstanceVersion" or "SequenceObjects" or "ObjName")
                {
                    continue;
                }
                switch (p)
                {
                    case ObjectProperty o: facts.Add($"{name}={pcc.GetEntry(o.Value)?.ObjectName.Instanced ?? "none"}"); break;
                    case NameProperty n: facts.Add($"{name}={n.Value.Instanced}"); break;
                    case StrProperty s: facts.Add($"{name}=\"{s.Value}\""); break;
                    case IntProperty i: facts.Add($"{name}={i.Value}"); break;
                    case StringRefProperty sr when InspectionContext.IsLe2: facts.Add($"{name}={sr.Value}"); break;
                    case FloatProperty f: facts.Add($"{name}={f.Value}"); break;
                    case BoolProperty b: facts.Add($"{name}={b.Value}"); break;
                    case EnumProperty e: facts.Add($"{name}={e.Value.Instanced}"); break;
                    case ArrayProperty<StrProperty> strings: facts.Add($"{name}=[{string.Join("; ", strings.Select(x => x.Value))}]"); break;
                }
            }
            var outs = new List<string>();
            foreach (StructProperty link in props.GetProp<ArrayProperty<StructProperty>>("OutputLinks") ?? new ArrayProperty<StructProperty>("OutputLinks"))
            {
                string desc = link.GetProp<StrProperty>("LinkDesc")?.Value ?? "";
                foreach (StructProperty target in link.GetProp<ArrayProperty<StructProperty>>("Links") ?? new ArrayProperty<StructProperty>("Links"))
                {
                    if (pcc.GetEntry(target.GetProp<ObjectProperty>("LinkedOp")?.Value ?? 0) is ExportEntry next)
                    {
                        outs.Add($"{desc}->{next.ObjectName.Instanced}[{target.GetProp<IntProperty>("InputLinkIdx")?.Value ?? 0}]");
                    }
                }
            }
            var vars = new List<string>();
            foreach (StructProperty link in props.GetProp<ArrayProperty<StructProperty>>("VariableLinks") ?? new ArrayProperty<StructProperty>("VariableLinks"))
            {
                string desc = link.GetProp<StrProperty>("LinkDesc")?.Value ?? "";
                foreach (ObjectProperty v in link.GetProp<ArrayProperty<ObjectProperty>>("LinkedVariables") ?? new ArrayProperty<ObjectProperty>("LinkedVariables"))
                {
                    vars.Add($"{desc}:{pcc.GetEntry(v.Value)?.ObjectName.Instanced}");
                }
            }
            writer.WriteLine($"{export.ClassName}\t{export.InstancedFullPath.Split("Main_Sequence.").Last()}\t{string.Join(", ", facts)}\tout: {string.Join(" ", outs)}\tvars: {string.Join(" ", vars)}");
            ++count;
        }
        Console.WriteLine($"{count} sequence objects -> {outPath}");
        return InspectionContext.IsLe2 && count == 0 ? 1 : 0;
    }
}
