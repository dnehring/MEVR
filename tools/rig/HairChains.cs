// SPDX-License-Identifier: GPL-3.0-only
// Hair chains for mevr-rig (tools/rig/README.md): not yet.
using System.Collections.Generic;
using LegendaryExplorerCore.Unreal.BinaryConverters;

namespace Mevr.Rig;

internal sealed class HairPlan
{
    public IEnumerable<BonePlan> Bones() => System.Array.Empty<BonePlan>();
}

internal static class HairChains
{
    public static void Build(SkeletalMesh mesh, HairPlan plan, List<int> added, Report report)
    {
    }
}
