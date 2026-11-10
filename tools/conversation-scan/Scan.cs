// SPDX-License-Identifier: GPL-3.0-only
// Every LE1 conversation from the game's packages, without running the game (research/59 "The scan"): one JSON line per
//   conversation   a BioConversation (the _LOC_INT packages hold them): its lines, speakers, camera intimacy, GUI styles, and its
//                  graph (the start entries, each entry's ambient flag, speaker and replies, each reply's type and entries), so
//                  the scan can tell a chain (a play that never reaches a wheel) from a conversation (research/59 "Flowing
//                  conversations")
//   start          a BioSeqAct_StartConversation in a level's script: which conversation, its owner (the actor the conversation
//                  belongs to: its class and, when it is placed in the level, its Location) and its stage (a BioStage: the marks
//                  the game puts the actors on, and the stage's cameras) or none (the actors stay where they stand); the launcher
//                  (tools/conversation-launch.ps1) opens the level, streams the chunk in and stands Shepard by the owner
//   volume         a BioTriggerStream (a streaming volume) and its states: the chunks each shows and loads, the world streaming
//                  state it needs (StateName; None is the plain touch) and the chunk it applies in; the catalogue says per start
//                  whether a touch shows its chunk or which named state the plot would set (mevr stream state <name> on)
//   level          a persistent level's streaming list (its WorldInfo's StreamingLevels: the packages it can stream in), so the
//                  catalogue names the level to open for a start by what the game streams, not by the package's name (the
//                  medical bay's wake-up scene, BIOA_NOR10_07wake_DSG, is streamed by the Citadel's BIOA_STA00)
//   vo             a BioSeqAct_FaceOnlyVO in a level's script: lines of a conversation played as voice-over, with no conversation
//                  mode (a squadmate's remark on foot, radio chatter, the lines under an animated cutscene: Saren and
//                  Benezia after Eden Prime are an ANIMCUTSCENE sequence with such actions); the sweep's predictions tell
//                  these from the conversations a level starts
//   matinee        every SeqAct_Interp of a level script, with its groups and tracks, whatever starts it
//   trigger        a trigger or trigger volume placed in a level (any class named *Trigger*, the streaming volumes too): its
//                  class and place, so the sweep's predictions can say which conversations play within a walk of one (the
//                  mod refuses a walk step that would change which triggers Shepard overlaps; research/69 item 5)
//   cinematic      a SeqAct_Interp a conversation node fires in a level's script (BioSeqEvt_ConvNode -> ... -> the matinee):
//                  its groups and tracks, so authored camera cuts (a director track) and actor moves (move tracks) show
// Reads only; writes nothing into the game. Its output is game data: keep it out of the repository.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using LegendaryExplorerCore;
using LegendaryExplorerCore.Packages;
using LegendaryExplorerCore.Unreal;

namespace Mevr.ConversationScan;

internal static class Scan
{
    public static int Run(string cooked, string outPath, string only)
    {
        LegendaryExplorerCoreLib.InitLib(TaskScheduler.Default, s => Console.Error.WriteLine("LEC: " + s), null, new[] { MEGame.LE1 });
        var files = Directory.GetFiles(cooked, "*.pcc").OrderBy(f => f).Where(f => Wanted(Path.GetFileNameWithoutExtension(f)));
        if (only != null)
        {
            files = files.Where(f => Path.GetFileName(f).Contains(only, StringComparison.OrdinalIgnoreCase));
        }
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        int packages = 0, lines = 0, failed = 0;
        using var writer = new StreamWriter(outPath);
        foreach (string file in files)
        {
            string package = Path.GetFileNameWithoutExtension(file);
            try
            {
                using IMEPackage pcc = MEPackageHandler.OpenMEPackage(file, forceLoadFromDisk: true);
                ++packages;
                foreach (ExportEntry export in pcc.Exports)
                {
                    Dictionary<string, object> record = export.ClassName switch
                    {
                        "BioConversation" => seen.Add(export.InstancedFullPath) ? Conversation(export) : null,
                        "BioSeqAct_StartConversation" => Start(pcc, export),
                        "BioSeqEvt_ConvNode" => Cinematic(pcc, export),
                        "BioTriggerStream" => Volume(export),
                        "BioSeqAct_FaceOnlyVO" => VoiceOver(pcc, export),
                        "SeqAct_Interp" => LevelMatinee(pcc, export),
                        "BioWorldInfo" or "WorldInfo" => Level(pcc, export),
                        _ => null,
                    };
                    if (record != null)
                    {
                        record["package"] = package;
                        writer.WriteLine(JsonSerializer.Serialize(record));
                        ++lines;
                    }
                    if (export.ClassName.Contains("Trigger", StringComparison.OrdinalIgnoreCase) && export.Parent?.ClassName == "Level"
                        && export.GetProperties().GetProp<StructProperty>("Location") is { } triggerAt)
                    {
                        writer.WriteLine(JsonSerializer.Serialize(new Dictionary<string, object>
                        {
                            ["kind"] = "trigger", ["name"] = export.InstancedFullPath, ["class"] = export.ClassName, ["package"] = package,
                            ["at"] = new[] { triggerAt.GetProp<FloatProperty>("X")?.Value ?? 0, triggerAt.GetProp<FloatProperty>("Y")?.Value ?? 0, triggerAt.GetProp<FloatProperty>("Z")?.Value ?? 0 },
                        }));
                        ++lines;
                    }
                }
            }
            catch (Exception e)
            {
                ++failed;
                Console.Error.WriteLine($"{package}: {e.GetType().Name}: {e.Message}");
            }
            if (packages % 200 == 0)
            {
                Console.Error.WriteLine($"{packages} packages, {lines} records");
            }
        }
        Console.WriteLine($"{packages} packages read ({failed} failed), {lines} records -> {outPath}");
        return 0;
    }

    // Conversations live in the English localised packages; starts, stages and matinees in the levels' own. The other
    // languages, the sound, art, lighting and texture packages hold none of them. (The placeables' packages, _PLC, do:
    // Ilos's archive door starts its conversation from BIOA_LOS40_01_PLC; they were skipped until 2026-11-09.)
    private static bool Wanted(string name)
    {
        string upper = name.ToUpperInvariant();
        if (upper.Contains("_LOC_"))
        {
            return upper.EndsWith("_LOC_INT");
        }
        return !(upper.EndsWith("_SND") || upper.EndsWith("_ART") || upper.EndsWith("_LIGHT") || upper.StartsWith("TEXTURES"));
    }

    private static Dictionary<string, object> Conversation(ExportEntry export)
    {
        PropertyCollection props = export.GetProperties();
        var entries = props.GetProp<ArrayProperty<StructProperty>>("m_EntryList");
        var replies = props.GetProp<ArrayProperty<StructProperty>>("m_ReplyList");
        var speakers = props.GetProp<ArrayProperty<StructProperty>>("m_SpeakerList");
        var intimacy = new SortedDictionary<int, int>();
        var styles = new SortedDictionary<string, int>();
        int ambient = 0, unskippable = 0;
        // The graph itself, for the opening run and the wheels (research/59 "Flowing conversations"): each entry's ambient flag,
        // speaker and the replies it offers; each reply's type (REPLY_STANDARD and REPLY_DIALOGEND show the wheel,
        // REPLY_AUTOCONTINUE goes on by itself), ambient flag and the entries it leads to.
        var nodes = new List<Dictionary<string, object>>();
        var replyNodes = new List<Dictionary<string, object>>();
        foreach (StructProperty node in entries ?? new ArrayProperty<StructProperty>("m_EntryList"))
        {
            int camera = node.GetProp<IntProperty>("nCameraIntimacy")?.Value ?? -1;
            intimacy[camera] = intimacy.GetValueOrDefault(camera) + 1;
            string style = node.GetProp<EnumProperty>("eGUIStyle")?.Value.Instanced ?? "?";
            styles[style] = styles.GetValueOrDefault(style) + 1;
            bool isAmbient = node.GetProp<BoolProperty>("bAmbient")?.Value == true;
            ambient += isAmbient ? 1 : 0;
            unskippable += node.GetProp<BoolProperty>("bSkippable")?.Value == false ? 1 : 0;
            nodes.Add(new Dictionary<string, object>
            {
                ["ambient"] = isAmbient,
                ["nonText"] = node.GetProp<BoolProperty>("bNonTextLine")?.Value == true,
                ["speaker"] = node.GetProp<IntProperty>("nSpeakerIndex")?.Value ?? -1,
                ["text"] = node.GetProp<IntProperty>("srText")?.Value ?? 0,
                ["conditional"] = node.GetProp<IntProperty>("nConditionalFunc")?.Value ?? -1,  // -1: none (a start entry always eligible)
                // The line's plot effects (research/69 item 1): the state transition it runs when it starts, and the Kismet
                // conversation-node event it fires (nScriptIndex into m_ScriptList; -1 none).
                ["transition"] = node.GetProp<IntProperty>("nStateTransition")?.Value ?? -1,
                ["script"] = node.GetProp<IntProperty>("nScriptIndex")?.Value ?? -1,
                ["replies"] = node.GetProp<ArrayProperty<StructProperty>>("ReplyListNew")?.Select(r => r.GetProp<IntProperty>("nIndex")?.Value ?? -1).ToList()
                              ?? new List<int>(),
            });
        }
        foreach (StructProperty node in replies ?? new ArrayProperty<StructProperty>("m_ReplyList"))
        {
            replyNodes.Add(new Dictionary<string, object>
            {
                ["type"] = node.GetProp<EnumProperty>("ReplyType")?.Value.Instanced ?? "REPLY_STANDARD",
                ["ambient"] = node.GetProp<BoolProperty>("bAmbient")?.Value == true,
                ["nonText"] = node.GetProp<BoolProperty>("bNonTextLine")?.Value == true,
                ["text"] = node.GetProp<IntProperty>("srText")?.Value ?? 0,
                ["conditional"] = node.GetProp<IntProperty>("nConditionalFunc")?.Value ?? -1,
                ["transition"] = node.GetProp<IntProperty>("nStateTransition")?.Value ?? -1,
                ["script"] = node.GetProp<IntProperty>("nScriptIndex")?.Value ?? -1,
                ["entries"] = node.GetProp<ArrayProperty<IntProperty>>("EntryList")?.Select(e => e.Value).ToList() ?? new List<int>(),
            });
        }
        return new Dictionary<string, object>
        {
            ["kind"] = "conversation",
            ["name"] = export.InstancedFullPath,
            ["entries"] = entries?.Count ?? 0,
            ["replies"] = replies?.Count ?? 0,
            ["speakers"] = speakers?.Select(s => s.GetProp<NameProperty>("sSpeakerTag")?.Value.Instanced ?? "?").ToList() ?? new List<string>(),
            ["intimacy"] = intimacy.ToDictionary(p => p.Key.ToString(), p => p.Value),
            ["styles"] = styles,
            ["ambient"] = ambient,
            ["unskippable"] = unskippable,
            ["starting"] = props.GetProp<ArrayProperty<IntProperty>>("m_StartingList")?.Select(e => e.Value).ToList() ?? new List<int>(),
            ["nodes"] = nodes,
            ["replyNodes"] = replyNodes,
        };
    }

    private static Dictionary<string, object> Start(IMEPackage pcc, ExportEntry export)
    {
        PropertyCollection props = export.GetProperties();
        IEntry conv = pcc.GetEntry(props.GetProp<ObjectProperty>("Conv")?.Value ?? 0);
        var record = new Dictionary<string, object>
        {
            ["kind"] = "start",
            ["path"] = export.InstancedFullPath,
            ["conv"] = conv?.InstancedFullPath,
            ["comment"] = props.GetProp<ArrayProperty<StrProperty>>("m_aObjComment")?.FirstOrDefault()?.Value,
        };
        foreach (ExportEntry variable in Linked(pcc, props, "Owner"))
        {
            // The owner link is a SeqVar_Object (its ObjValue the actor) or a SeqVar_Player / a named variable (found at run time).
            PropertyCollection vp = variable.GetProperties();
            ExportEntry owner = pcc.GetEntry(vp.GetProp<ObjectProperty>("ObjValue")?.Value ?? 0) as ExportEntry;
            record["ownerVar"] = variable.ClassName;
            if (vp.GetProp<NameProperty>("m_sObjectTagToFind") is { } findTag)
            {
                record["ownerTag"] = findTag.Value.Instanced;  // BioSeqVar_ObjectFindByTag: the actor by its Tag at run time
            }
            if (owner == null)
            {
                continue;
            }
            record["owner"] = owner.InstancedFullPath;
            record["ownerClass"] = owner.ClassName;
            PropertyCollection op = owner.GetProperties();
            if (op.GetProp<StructProperty>("Location") is { } at)
            {
                record["ownerAt"] = new[] { at.GetProp<FloatProperty>("X")?.Value ?? 0, at.GetProp<FloatProperty>("Y")?.Value ?? 0, at.GetProp<FloatProperty>("Z")?.Value ?? 0 };
            }
            if (op.GetProp<StructProperty>("Rotation") is { } rot)
            {
                record["ownerYaw"] = (rot.GetProp<IntProperty>("Yaw")?.Value ?? 0) * 360.0 / 65536.0;
            }
            if (op.GetProp<NameProperty>("Tag") is { } tag)
            {
                record["ownerTag"] = tag.Value.Instanced;
            }
        }
        foreach (ExportEntry variable in Linked(pcc, props, "Stage"))
        {
            ExportEntry stage = pcc.GetEntry(variable.GetProperties().GetProp<ObjectProperty>("ObjValue")?.Value ?? 0) as ExportEntry;
            if (stage == null)
            {
                continue;
            }
            PropertyCollection stageProps = stage.GetProperties();
            record["stage"] = stage.InstancedFullPath;
            record["stageType"] = pcc.GetEntry(stageProps.GetProp<ObjectProperty>("Type")?.Value ?? 0)?.InstancedFullPath;
            record["stageCameras"] = stageProps.GetProp<ArrayProperty<StructProperty>>("m_CameraCustomList")?.Count ?? 0;
            if (stageProps.GetProp<StructProperty>("Location") is { } stageAt)
            {
                record["stageAt"] = new[] { stageAt.GetProp<FloatProperty>("X")?.Value ?? 0, stageAt.GetProp<FloatProperty>("Y")?.Value ?? 0, stageAt.GetProp<FloatProperty>("Z")?.Value ?? 0 };
            }
        }
        return record;
    }

    // A streaming volume and its states (BioTriggerStream.StreamingStates): from every scanned package (the persistent one and
    // the chunks' own); the catalogue groups them by the persistent level.
    private static Dictionary<string, object> Volume(ExportEntry export)
    {
        var states = new List<object>();
        foreach (StructProperty state in export.GetProperties().GetProp<ArrayProperty<StructProperty>>("StreamingStates") ?? new ArrayProperty<StructProperty>("StreamingStates"))
        {
            states.Add(new Dictionary<string, object>
            {
                ["state"] = state.GetProp<NameProperty>("StateName")?.Value.Instanced,
                ["in"] = state.GetProp<NameProperty>("InChunkName")?.Value.Instanced,
                ["visible"] = Names(state, "VisibleChunkNames"),
                ["load"] = Names(state, "LoadChunkNames"),
            });
        }
        return new Dictionary<string, object> { ["kind"] = "volume", ["name"] = export.InstancedFullPath, ["states"] = states };
    }

    // Every matinee of a level script, with its groups and tracks ("matinee" records): the authored moments around scenes
    // (Anderson walking Shepard to the council hearing is one, in cinematic mode, before the conversation starts).
    private static Dictionary<string, object> LevelMatinee(IMEPackage pcc, ExportEntry export)
    {
        Dictionary<string, object> record = Matinee(pcc, export);
        record["kind"] = "matinee";
        return record;
    }

    private static Dictionary<string, object> VoiceOver(IMEPackage pcc, ExportEntry export)
    {
        IEntry conv = pcc.GetEntry(export.GetProperties().GetProp<ObjectProperty>("m_pConversation")?.Value ?? 0);
        return conv == null ? null : new Dictionary<string, object> { ["kind"] = "vo", ["path"] = export.InstancedFullPath, ["conv"] = conv.InstancedFullPath };
    }

    // A level's streaming list: the packages its WorldInfo can stream in (LevelStreaming objects' PackageName). A chunk's own
    // WorldInfo has none and gives no record.
    private static Dictionary<string, object> Level(IMEPackage pcc, ExportEntry export)
    {
        var streaming = new List<string>();
        foreach (ObjectProperty level in export.GetProperties().GetProp<ArrayProperty<ObjectProperty>>("StreamingLevels") ?? new ArrayProperty<ObjectProperty>("StreamingLevels"))
        {
            if (pcc.GetEntry(level.Value) is ExportEntry entry && entry.GetProperties().GetProp<NameProperty>("PackageName") is { } name)
            {
                streaming.Add(name.Value.Instanced);
            }
        }
        return streaming.Count == 0 ? null : new Dictionary<string, object> { ["kind"] = "level", ["name"] = export.InstancedFullPath, ["streaming"] = streaming };
    }

    private static List<string> Names(StructProperty owner, string property)
    {
        return (owner.GetProp<ArrayProperty<NameProperty>>(property) ?? new ArrayProperty<NameProperty>(property)).Select(n => n.Value.Instanced).ToList();
    }

    // A conversation node's event in a level's script and the matinees it fires (followed through up to 4 actions).
    private static Dictionary<string, object> Cinematic(IMEPackage pcc, ExportEntry export)
    {
        PropertyCollection props = export.GetProperties();
        var simple = new Dictionary<string, object>();
        foreach (Property p in props)
        {
            switch (p)
            {
                case IntProperty i: simple[p.Name.Instanced] = i.Value; break;
                case NameProperty n: simple[p.Name.Instanced] = n.Value.Instanced; break;
                case StrProperty s: simple[p.Name.Instanced] = s.Value; break;
                case BoolProperty b: simple[p.Name.Instanced] = b.Value; break;
                case ObjectProperty o when p.Name.Instanced != "ParentSequence": simple[p.Name.Instanced] = pcc.GetEntry(o.Value)?.InstancedFullPath; break;
            }
        }
        var matinees = new List<object>();
        var visited = new HashSet<int>();
        var frontier = new List<ExportEntry> { export };
        for (int depth = 0; depth < 4 && frontier.Count > 0; ++depth)
        {
            var next = new List<ExportEntry>();
            foreach (ExportEntry op in frontier)
            {
                foreach (ExportEntry linked in Outputs(pcc, op).Where(e => visited.Add(e.UIndex)))
                {
                    if (linked.ClassName == "SeqAct_Interp")
                    {
                        matinees.Add(Matinee(pcc, linked));
                    }
                    else
                    {
                        next.Add(linked);
                    }
                }
            }
            frontier = next;
        }
        return new Dictionary<string, object> { ["kind"] = "cinematic", ["path"] = export.InstancedFullPath, ["props"] = simple, ["matinees"] = matinees };
    }

    private static Dictionary<string, object> Matinee(IMEPackage pcc, ExportEntry interp)
    {
        var groups = new List<object>();
        float length = 0;
        foreach (ExportEntry data in Linked(pcc, interp.GetProperties(), "Data").Where(e => e.ClassName == "InterpData"))
        {
            PropertyCollection dataProps = data.GetProperties();
            length = dataProps.GetProp<FloatProperty>("InterpLength")?.Value ?? 0;
            foreach (ObjectProperty g in dataProps.GetProp<ArrayProperty<ObjectProperty>>("InterpGroups") ?? new ArrayProperty<ObjectProperty>("InterpGroups"))
            {
                if (pcc.GetEntry(g.Value) is not ExportEntry group)
                {
                    continue;
                }
                PropertyCollection groupProps = group.GetProperties();
                var tracks = (groupProps.GetProp<ArrayProperty<ObjectProperty>>("InterpTracks") ?? new ArrayProperty<ObjectProperty>("InterpTracks"))
                    .Select(t => pcc.GetEntry(t.Value)?.ClassName ?? "?").ToList();
                groups.Add(new Dictionary<string, object>
                {
                    ["class"] = group.ClassName,
                    ["name"] = groupProps.GetProp<NameProperty>("GroupName")?.Value.Instanced,
                    ["tracks"] = tracks,
                });
            }
        }
        return new Dictionary<string, object> { ["path"] = interp.InstancedFullPath, ["length"] = length, ["groups"] = groups };
    }

    private static IEnumerable<ExportEntry> Outputs(IMEPackage pcc, ExportEntry op)
    {
        foreach (StructProperty link in op.GetProperties().GetProp<ArrayProperty<StructProperty>>("OutputLinks") ?? new ArrayProperty<StructProperty>("OutputLinks"))
        {
            foreach (StructProperty target in link.GetProp<ArrayProperty<StructProperty>>("Links") ?? new ArrayProperty<StructProperty>("Links"))
            {
                if (pcc.GetEntry(target.GetProp<ObjectProperty>("LinkedOp")?.Value ?? 0) is ExportEntry next)
                {
                    yield return next;
                }
            }
        }
    }

    private static IEnumerable<ExportEntry> Linked(IMEPackage pcc, PropertyCollection props, string linkDesc)
    {
        foreach (StructProperty link in props.GetProp<ArrayProperty<StructProperty>>("VariableLinks") ?? new ArrayProperty<StructProperty>("VariableLinks"))
        {
            if (!string.Equals(link.GetProp<StrProperty>("LinkDesc")?.Value, linkDesc, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }
            foreach (ObjectProperty v in link.GetProp<ArrayProperty<ObjectProperty>>("LinkedVariables") ?? new ArrayProperty<ObjectProperty>("LinkedVariables"))
            {
                if (pcc.GetEntry(v.Value) is ExportEntry variable)
                {
                    yield return variable;
                }
            }
        }
    }
}
