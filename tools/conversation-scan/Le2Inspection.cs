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

    internal static bool IsShepardGroup(string name) =>
        name != null && System.Text.RegularExpressions.Regex.IsMatch(name, @"^(pc_|hm[mf]_pc|shep|player$|pc$)", System.Text.RegularExpressions.RegexOptions.IgnoreCase);

    // ME2 moves people inside a conversation two ways (research/le2-conversations-in-world.md "How ME2 moves people"): a move
    // track on an actor's group (InterpTrackMove, "where to": its first and last key), and a gesture track whose animation walks
    // with root motion (BioEvtSysTrackGesture: WalktoNode_Forward, Wander_LongWalkEnter, DL_EnterWalk5p_Turn90R ...). Names only.
    static readonly System.Text.RegularExpressions.Regex WalkAnim =
        new(@"walk|step|wander|pace|stroll|run_|_run|turn", System.Text.RegularExpressions.RegexOptions.IgnoreCase);

    internal static void ExtendGroup(IMEPackage pcc, Dictionary<string, object> group, PropertyCollection groupProps)
    {
        var walks = new List<Dictionary<string, object>>();
        var moves = new List<Dictionary<string, object>>();
        foreach (ObjectProperty t in groupProps.GetProp<ArrayProperty<ObjectProperty>>("InterpTracks") ?? new ArrayProperty<ObjectProperty>("InterpTracks"))
        {
            if (pcc.GetEntry(t.Value) is not ExportEntry track)
                continue;
            PropertyCollection tp = track.GetProperties();
            if (track.ClassName == "BioEvtSysTrackGesture")
            {
                var anims = new List<string>();
                foreach (StructProperty g in tp.GetProp<ArrayProperty<StructProperty>>("m_aGestures") ?? new ArrayProperty<StructProperty>("m_aGestures"))
                {
                    foreach (string field in new[] { "nmGestureAnim", "nmTransitionAnim", "nmPoseAnim" })
                    {
                        string name = g.GetProp<NameProperty>(field)?.Value.Instanced;
                        if (name != null && name != "None" && WalkAnim.IsMatch(name) && !anims.Contains(name))
                            anims.Add(name);
                    }
                }
                if (anims.Count > 0)
                    walks.Add(new Dictionary<string, object> { ["actor"] = tp.GetProp<NameProperty>("m_nmFindActor")?.Value.Instanced, ["anims"] = anims });
            }
            else if (track.ClassName is "InterpTrackMove" or "BioInterpTrackMove")
            {
                var points = tp.GetProp<StructProperty>("PosTrack")?.GetProp<ArrayProperty<StructProperty>>("Points");
                if (points == null || points.Count == 0)
                    continue;
                float[] At(StructProperty point)
                {
                    var v = point.GetProp<StructProperty>("OutVal");
                    return new[] { v?.GetProp<FloatProperty>("X")?.Value ?? 0, v?.GetProp<FloatProperty>("Y")?.Value ?? 0, v?.GetProp<FloatProperty>("Z")?.Value ?? 0 };
                }
                float[] a = At(points[0]), b = At(points[points.Count - 1]);
                double span = 0;
                for (int i = 1; i < points.Count; ++i)
                {
                    float[] p0 = At(points[i - 1]), p1 = At(points[i]);
                    span += Math.Sqrt((p1[0] - p0[0]) * (p1[0] - p0[0]) + (p1[1] - p0[1]) * (p1[1] - p0[1]) + (p1[2] - p0[2]) * (p1[2] - p0[2]));
                }
                moves.Add(new Dictionary<string, object>
                {
                    ["keys"] = points.Count,
                    ["from"] = a, ["to"] = b,
                    ["netUnits"] = Math.Round(Math.Sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) + (b[2] - a[2]) * (b[2] - a[2])), 1),
                    ["pathUnits"] = Math.Round(span, 1),
                    ["frame"] = tp.GetProp<EnumProperty>("MoveFrame")?.Value.Instanced,
                });
            }
        }
        if (walks.Count > 0)
            group["walkGestures"] = walks;
        if (moves.Count > 0)
            group["moves"] = moves;
    }
}
