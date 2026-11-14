// SPDX-License-Identifier: MIT
// Synthetic property fixtures only; no game packages or dialogue text are embedded.
using System;
using System.Collections.Generic;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.Unreal;

namespace Mevr.ConversationScan;

internal static class InspectionTests
{
    internal static int Run()
    {
        InspectionContext.Initialize();
        int checks = 0;
        void Check(bool condition, string description)
        {
            ++checks;
            if (!condition) throw new InvalidOperationException("Inspection test failed: " + description);
        }
        var entry = new PropertyCollection
        {
            new StringRefProperty(4567, "srText"), new IntProperty(99, "nExportID"),
            new IntProperty(-2, "nStateTransitionParam"),
            new ArrayProperty<StructProperty>(new[]
            {
                new StructProperty("BioDialogReplyListDetails", new PropertyCollection
                {
                    new IntProperty(4, "nIndex"), new StringRefProperty(7654, "srParaphrase"),
                    new EnumProperty("REPLY_CATEGORY_RENEGADE_INTERRUPT", "EReplyCategory", MEGame.LE2, "Category"),
                }),
            }, "ReplyListNew"),
        };
        Check(Le2Inspection.StringRef(entry, "srText") == 4567, "LE2 string reference preserved");
        Check(Le2Inspection.StringRef(new PropertyCollection { new IntProperty(1234, "srText") }, "srText") == 1234,
            "integer string reference fallback preserved");
        Check(Le2Inspection.StringRef(new PropertyCollection(), "srText") == 0, "absent text has no invented ID");
        var node = new Dictionary<string, object> { ["text"] = 0, ["replies"] = new List<int> { 4 } };
        Le2Inspection.ExtendNode(node, entry, true);
        var edges = (List<Dictionary<string, object>>)node["replyEdges"];
        Check((int)node["text"] == 4567 && (int)node["exportId"] == 99 && (int)node["transitionParam"] == -2,
            "node identity and transition parameter preserved");
        Check((int)edges[0]["index"] == 4 && (int)edges[0]["paraphrase"] == 7654
            && (string)edges[0]["category"] == "REPLY_CATEGORY_RENEGADE_INTERRUPT", "interrupt edge remains distinguishable");
        Check(((List<int>)node["replies"])[0] == 4, "legacy adjacency remains unchanged");

        const float precise = 1.2345678f;
        var track = new PropertyCollection
        {
            new ArrayProperty<StructProperty>(new[]
            {
                new StructProperty("TrackKey", new PropertyCollection { new FloatProperty(precise, "fTime") }),
                new StructProperty("TrackKey", new PropertyCollection { new FloatProperty(2.75f, "fTime") }),
            }, "m_aTrackKeys"),
            new ArrayProperty<StructProperty>(new[]
            {
                new StructProperty("Interrupt", new PropertyCollection { new BoolProperty(true, "bShowInterrupt") }),
                new StructProperty("Interrupt", new PropertyCollection { new BoolProperty(false, "bShowInterrupt") }),
            }, "m_aInterruptData"),
        };
        var keys = Le2Inspection.InterruptKeys(track, out var warnings);
        Check(keys.Count == 2 && warnings.Count == 0, "complete key arrays pair by original index");
        Check((float)keys[0]["timeSeconds"] == precise && (bool)keys[0]["showInterrupt"]
            && !(bool)keys[1]["showInterrupt"], "precision and both visibility edges survive");
        track.GetProp<ArrayProperty<StructProperty>>("m_aInterruptData").RemoveAt(1);
        keys = Le2Inspection.InterruptKeys(track, out warnings);
        Check(keys.Count == 2 && keys[1]["showInterrupt"] == null && warnings.Count > 0,
            "unequal arrays retain unknown visibility rather than false");
        var missing = Le2Inspection.InterruptKeys(new PropertyCollection(), out warnings);
        Check(missing.Count == 0 && warnings.Count > 0, "missing arrays cannot fabricate a window");
        track.GetProp<ArrayProperty<StructProperty>>("m_aTrackKeys")[0].Properties.AddOrReplaceProp(new FloatProperty(float.NaN, "fTime"));
        keys = Le2Inspection.InterruptKeys(track, out warnings);
        Check(keys[0]["timeSeconds"] == null && warnings.Count > 0, "nonfinite time cannot become evidence");
        Check(Le2Inspection.SequencePath("Test.Node_Data_Sequence.InterpData_1.Track_0", true), "conversation sequence admitted");
        Check(!Le2Inspection.SequencePath("Test.Node_Data_Sequence.InterpData_1.Track_0", false), "LE1 sequence scope retained");
        Check(!Le2Inspection.SequencePath("Test.Not_Node_Data_Sequence.Track_0", true), "partial-name false match excluded");
        Check(Le2Inspection.SequencePath("Test.Main_Sequence.Action_0", false), "level sequence retained");
        Console.WriteLine($"{checks} synthetic inspection checks passed (no native acceptance).");
        return 0;
    }
}
