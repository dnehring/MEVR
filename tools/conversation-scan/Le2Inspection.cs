// SPDX-License-Identifier: MIT
// Authored data inspection only: visibility keys are not measured runtime acceptance windows.
using System;
using System.Collections.Generic;
using System.Linq;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.Unreal;

namespace Mevr.ConversationScan;

internal static class Le2Inspection
{
    internal static int StringRef(PropertyCollection props, string name) =>
        props.GetProp<StringRefProperty>(name)?.Value ?? props.GetProp<IntProperty>(name)?.Value ?? 0;

    internal static List<Dictionary<string, object>> ReplyEdges(PropertyCollection props) =>
        (props.GetProp<ArrayProperty<StructProperty>>("ReplyListNew") ?? new ArrayProperty<StructProperty>("ReplyListNew"))
        .Select(edge => new Dictionary<string, object>
        {
            ["index"] = edge.GetProp<IntProperty>("nIndex")?.Value ?? -1,
            ["category"] = edge.GetProp<EnumProperty>("Category")?.Value.Instanced,
            ["paraphrase"] = StringRef(edge.Properties, "srParaphrase"),
        }).ToList();

    internal static void ExtendNode(Dictionary<string, object> record, PropertyCollection props, bool entry)
    {
        record["text"] = StringRef(props, "srText");
        record["exportId"] = props.GetProp<IntProperty>("nExportID")?.Value;
        record["transitionParam"] = props.GetProp<IntProperty>("nStateTransitionParam")?.Value;
        if (entry)
            record["replyEdges"] = ReplyEdges(props);
    }

    internal static List<Dictionary<string, object>> InterruptKeys(PropertyCollection props, out List<string> warnings)
    {
        var data = props.GetProp<ArrayProperty<StructProperty>>("m_aInterruptData");
        var times = props.GetProp<ArrayProperty<StructProperty>>("m_aTrackKeys");
        warnings = new List<string>();
        if (data == null || times == null || data.Count != times.Count)
            warnings.Add("missing or unequal interrupt data/key arrays; no complete window inferred");
        var keys = new List<Dictionary<string, object>>();
        for (int i = 0; i < Math.Max(data?.Count ?? 0, times?.Count ?? 0); ++i)
        {
            float? time = i < (times?.Count ?? 0) ? times[i].GetProp<FloatProperty>("fTime")?.Value : null;
            bool? show = i < (data?.Count ?? 0) ? data[i].GetProp<BoolProperty>("bShowInterrupt")?.Value : null;
            if (time.HasValue && !float.IsFinite(time.Value))
            {
                warnings.Add($"nonfinite time at key {i}");
                time = null;
            }
            if (!time.HasValue || !show.HasValue)
                warnings.Add($"missing time or visibility at key {i}");
            keys.Add(new Dictionary<string, object> { ["index"] = i, ["timeSeconds"] = time, ["showInterrupt"] = show });
        }
        return keys;
    }

    internal static bool SequencePath(string path, bool includeConversation) =>
        path.Split('.').Any(part => part == "Main_Sequence" || (includeConversation && part == "Node_Data_Sequence"));

    internal static Dictionary<string, object> Identity(IEntry entry) => entry == null ? null :
        new Dictionary<string, object> { ["path"] = entry.InstancedFullPath, ["uIndex"] = entry.UIndex };

    internal static Dictionary<string, object> Interrupt(IMEPackage pcc, ExportEntry export)
    {
        var keys = InterruptKeys(export.GetProperties(), out var warnings);
        IEntry data = null, sequence = null;
        for (IEntry parent = export.Parent; parent != null; parent = parent.Parent)
        {
            if (parent.ClassName == "InterpData" && data == null) data = parent;
            if (parent.ObjectName.Name == "Node_Data_Sequence") sequence = parent;
        }
        var conversations = pcc.Exports.Where(e => e.ClassName == "BioConversation"
            && sequence != null && e.GetProperties().GetProp<ObjectProperty>("MatineeSequence")?.Value == sequence.UIndex)
            .Select(e => Identity(e)).ToList();
        if (conversations.Count == 0) warnings.Add("no conversation MatineeSequence link resolved in this package");
        return new Dictionary<string, object>
        {
            ["kind"] = "interrupt", ["path"] = export.InstancedFullPath, ["exportIndex"] = export.UIndex,
            ["interpData"] = Identity(data), ["group"] = Identity(export.Parent),
            ["sequence"] = Identity(sequence), ["conversations"] = conversations,
            ["keys"] = keys, ["warnings"] = warnings,
            ["scope"] = "authored visibility keys; runtime window and input acceptance unverified",
        };
    }
}
