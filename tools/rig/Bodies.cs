// SPDX-License-Identifier: GPL-3.0-only
// mevr-rig bodies --game LE1|LE2|LE3 [--game-root <Mass Effect Legendary Edition folder>] [--out <folder>] [--plans <folder>]
//                 [--ref <ME3's MEVR_Bodies_LE3.pcc>]
//
// The players' build (tools/rig/README.md "The players' build", docs/trilogy-hair-plan.md section 12): everything a game's rigged
// bodies need, from the player's own game files, in one run and without Python or the census: plans/players.jsonc names the
// game's plans; its `bodies` plan is built (Rigging.Build), then each body's breast limit is measured against ME3 Liara's romance
// body (Sizes.Compute; for ME1 and ME2 that one body is built from ME3's files first, into <out>\reference, unless --ref names
// ME3's package already built), then the clubs' copies of the plan's "clubs" rigs are baked (Clubs.Make), then the `also` plans
// (Miranda's hair) are built. A step, a plan or a stage that fails is reported and left out; the rest stands.
//
// Into --out (default %LOCALAPPDATA%\MEVR\rig\<GAME>): each package, the bodies package's index (MEVR_Bodies_<GAME>.json),
// colliders\, and bodies.json (`mevr-rig-bodies`, version 1: the packages with their sizes, the counts, the time), which MEVR
// Setup reads to install the packages. Progress lines for the setup's bar: `progress <percent> <text>`. Exit 0 with the bodies
// package built, 3 when nothing could be built.
// Uses Legendary Explorer's library (GPL-3.0). Reads the player's game files; writes nothing in the game folder.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;
using LegendaryExplorerCore.Packages;

namespace Mevr.Rig;

internal static class Bodies
{
    public const string ResultFile = "bodies.json";

    public static int Run(string[] args)
    {
        string game = (Program.Arg(args, "--game") ?? "").ToUpperInvariant();
        if (game is not ("LE1" or "LE2" or "LE3"))
        {
            Console.Error.WriteLine("mevr-rig bodies --game LE1|LE2|LE3 [--game-root <folder>] [--out <folder>] [--plans <folder>] [--ref <MEVR_Bodies_LE3.pcc>]");
            return 2;
        }
        var started = System.Diagnostics.Stopwatch.StartNew();
        string plansDir = PlansDir(Program.Arg(args, "--plans"));
        string outDir = Path.GetFullPath(Program.Arg(args, "--out")
            ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MEVR", "rig", game));
        Directory.CreateDirectory(outDir);
        File.Delete(Path.Combine(outDir, ResultFile));
        var players = JsonNode.Parse(File.ReadAllText(Path.Combine(plansDir, "players.jsonc")),
            documentOptions: new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip, AllowTrailingCommas = true });
        var mine = players["games"][game];
        string bodiesPlanPath = Path.Combine(plansDir, mine["bodies"].GetValue<string>());
        var alsoPaths = mine["also"]?.AsArray().Select(n => Path.Combine(plansDir, n.GetValue<string>())).ToList() ?? new List<string>();
        string refGame = players["sizesReference"]["game"].GetValue<string>();
        string refRig = players["sizesReference"]["rig"].GetValue<string>();

        var bodiesPlan = Rigging.ReadPlan(bodiesPlanPath);
        var alsoPlans = alsoPaths.Select(p => (path: p, plan: Rigging.ReadPlan(p))).ToList();
        string givenRef = Program.Arg(args, "--ref");
        bool buildRef = game != refGame && (givenRef == null || !File.Exists(givenRef));
        int total = bodiesPlan.Steps.Count + alsoPlans.Sum(a => a.plan.Steps.Count) + (buildRef ? 1 : 0) + 2;
        int done = 0;
        void Step(string text)
        {
            ++done;
            Progress(done, total, text);
        }
        Progress(0, total, "reading " + Path.GetFileName(bodiesPlanPath));

        var packages = new List<string>();
        int failed = 0;
        // 1. Every female body.
        BuildResult bodies = null;
        try
        {
            bodies = Rigging.Build(bodiesPlan, bodiesPlanPath, new BuildOptions
            {
                Game = Lex.Parse(game), Args = args, OutDir = outDir, Progress = name => Step("body " + name),
            });
            failed += bodies.Failed;
        }
        catch (Exception e)
        {
            Console.WriteLine($"bodies: the bodies plan FAILED, {e.GetType().Name}: {e.Message}\n{e.StackTrace}");
        }
        if (bodies != null && bodies.Built > 0 && File.Exists(bodies.OutPath))
        {
            packages.Add(bodies.OutPath);
            // 2. The breasts' limits against the reference body.
            string referenceDir = Path.Combine(outDir, "reference");
            try
            {
                string refPackage = game == refGame ? bodies.OutPath : givenRef;
                if (buildRef)
                {
                    refPackage = BuildReference(plansDir, players, refGame, refRig, referenceDir, args);
                    Step("the reference body " + refRig);
                }
                Progress(done, total, "the breasts' limits");
                int sized = Sizes.Compute(bodies.OutPath, refPackage, refRig, true);
                Console.WriteLine($"bodies: breastMax for {sized} bodies against {refRig} in {refPackage}");
            }
            catch (Exception e)
            {
                Console.WriteLine($"bodies: the sizes FAILED (the index has no breastMax; the game uses its default), {e.GetType().Name}: {e.Message}\n{e.StackTrace}");
            }
            finally
            {
                if (Directory.Exists(referenceDir)) Directory.Delete(referenceDir, true);
            }
            Step("the breasts' limits");
            // 3. The clubs' copies, of the plan's club rigs that were built.
            try
            {
                var built = BuiltNames(Path.ChangeExtension(bodies.OutPath, ".json"));
                var names = bodiesPlan.Clubs.Where(built.Contains).ToList();
                foreach (string missing in bodiesPlan.Clubs.Where(n => !built.Contains(n)))
                {
                    Console.WriteLine($"bodies: club rig {missing} wasn't built: no club copy");
                }
                if (names.Count > 0)
                {
                    Progress(done, total, "the clubs' bodies");
                    var (made, notFound) = Clubs.Make(bodies.OutPath, names, Clubs.Middle, Clubs.Glute, Sizes.Shape.Breasts, Sizes.Shape.Glutes);
                    failed += notFound;
                }
            }
            catch (Exception e)
            {
                Console.WriteLine($"bodies: the club copies FAILED, {e.GetType().Name}: {e.Message}\n{e.StackTrace}");
                ++failed;
            }
            Step("the clubs' bodies");
        }
        else
        {
            Console.WriteLine("bodies: no body was built");
            done += 2 + (buildRef ? 1 : 0);
        }
        // 4. The other plans (hair stand-ins).
        foreach (var (path, plan) in alsoPlans)
        {
            try
            {
                var r = Rigging.Build(plan, path, new BuildOptions { Game = Lex.Parse(game), Args = args, OutDir = outDir, Progress = name => Step(name) });
                failed += r.Failed;
                if (r.Failed == 0 && r.Built > 0)
                {
                    packages.Add(r.OutPath);
                }
                else
                {
                    Console.WriteLine($"bodies: {Path.GetFileName(path)}: not complete, left out");
                    File.Delete(r.OutPath);
                    File.Delete(Path.ChangeExtension(r.OutPath, ".json"));
                }
            }
            catch (Exception e)
            {
                Console.WriteLine($"bodies: {Path.GetFileName(path)} FAILED, left out, {e.GetType().Name}: {e.Message}");
                string outPath = Path.Combine(outDir, Path.GetFileName(Rigging.Resolve(plan.Out, path, args)));
                File.Delete(outPath);
                File.Delete(Path.ChangeExtension(outPath, ".json"));
                ++failed;
            }
        }
        WriteResult(Path.Combine(outDir, ResultFile), game, packages, bodies, failed, started.Elapsed.TotalSeconds);
        Progress(total, total, packages.Count > 0 ? "done" : "nothing built");
        Console.WriteLine(string.Create(CultureInfo.InvariantCulture,
            $"bodies {game}: {packages.Count} packages ({string.Join(", ", packages.Select(Path.GetFileName))}), {failed} failed, {started.Elapsed.TotalSeconds:F0} s -> {outDir}"));
        return bodies != null && packages.Contains(bodies.OutPath) ? 0 : 3;
    }

    // The plans: --plans, else plans\ next to the exe (the players' build), else the repository's (tools/rig/bin/out -> tools/rig/plans).
    static string PlansDir(string given)
    {
        foreach (string dir in new[] { given, Path.Combine(AppContext.BaseDirectory, "plans"), Path.Combine(AppContext.BaseDirectory, "..", "..", "plans") })
        {
            if (dir != null && File.Exists(Path.Combine(dir, "players.jsonc")))
            {
                return Path.GetFullPath(dir);
            }
        }
        throw new FileNotFoundException("no plans folder with players.jsonc: pass --plans <folder>");
    }

    // ME3's reference body alone (the `rig` step of ME3's bodies plan, with that plan's skirt reference), into `dir`.
    static string BuildReference(string plansDir, JsonNode players, string refGame, string refRig, string dir, string[] args)
    {
        string planPath = Path.Combine(plansDir, players["games"][refGame]["bodies"].GetValue<string>());
        var full = Rigging.ReadPlan(planPath);
        var step = full.Steps.FirstOrDefault(s => string.Equals(s.Name, refRig, StringComparison.OrdinalIgnoreCase))
            ?? throw new InvalidOperationException($"{Path.GetFileName(planPath)} has no step {refRig}");
        step.Colliders = false;
        var plan = new Plan { Out = $"MEVR_Reference_{refGame}.pcc", FitReference = full.FitReference, Steps = new List<StepPlan> { step } };
        Console.WriteLine($"bodies: building the reference body {refRig} from {refGame}'s files");
        var r = Rigging.Build(plan, planPath, new BuildOptions { Game = Lex.Parse(refGame), Args = args, OutDir = dir });
        if (r.Built != 1) throw new InvalidOperationException($"the reference body {refRig} wasn't built");
        return r.OutPath;
    }

    static HashSet<string> BuiltNames(string indexPath)
    {
        var root = JsonNode.Parse(File.ReadAllText(indexPath));
        return root["meshes"].AsArray().Select(e => e["name"].GetValue<string>()).ToHashSet(StringComparer.OrdinalIgnoreCase);
    }

    static void Progress(int done, int total, string text)
    {
        int percent = total > 0 ? Math.Clamp(done * 100 / total, 0, 100) : 0;
        Console.WriteLine($"progress {percent} {text}");
        Console.Out.Flush();
    }

    static void WriteResult(string path, string game, List<string> packages, BuildResult bodies, int failed, double seconds)
    {
        var o = new JsonObject
        {
            ["format"] = "mevr-rig-bodies",
            ["version"] = 1,
            ["game"] = game,
            ["packages"] = new JsonArray(packages.Select(p => (JsonNode)new JsonObject
            {
                ["file"] = Path.GetFileName(p),
                ["size"] = new FileInfo(p).Length,
            }).ToArray()),
            ["bodies"] = bodies?.Built ?? 0,
            ["skipped"] = bodies?.Skipped ?? 0,
            ["failed"] = failed,
            ["seconds"] = Math.Round(seconds),
        };
        File.WriteAllText(path, o.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) + "\n");
    }
}
