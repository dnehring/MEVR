# Research log

Raw findings behind the plans in `docs/`. Each file records what was found, where it came from,
and what is still unverified. The [docs/](../docs) folder holds the conclusions.

| File | Topic | Date |
|------|-------|------|
| [01-feasibility.md](01-feasibility.md) | Can ME LE get VR at all? Why "build in UDK and port it" fails, and what exists today | 2026-10-07 |
| [02-titanfall2-vr.md](02-titanfall2-vr.md) | CircuitLord's Titanfall 2 VR mod | 2026-10-07 |
| [03-overlord-mw2cr-vr.md](03-overlord-mw2cr-vr.md) | Aeka0's Overlord (MW2 Campaign Remastered VR): features and architecture | 2026-10-07 |
| [04-me-le-modding-ecosystem.md](04-me-le-modding-ecosystem.md) | ME3Tweaks, LEBinkProxy, LExASIs, LExSDKv2, Legendary Explorer, the UDK workflow | 2026-10-07 |
| [05-vr-frameworks.md](05-vr-frameworks.md) | UEVR, REFramework | 2026-10-07 |
| [06-repo-licenses.md](06-repo-licenses.md) | License and size check for every reference repo (GitHub API) | 2026-10-07 |
| [07-machine-audit.md](07-machine-audit.md) | Dev machine: toolchain, GPU, headset runtime, game install | 2026-10-07 |
| [08-ue3-hypotheses.md](08-ue3-hypotheses.md) | UE3 engine mechanisms we plan to lean on (all unverified against ME LE) | 2026-10-07 |
| [09-me3tweaks-code-survey.md](09-me3tweaks-code-survey.md) | LExASIs, LExSDKv2 and LEBinkProxy, verified from cloned source | 2026-10-07 |
| [10-vr-code-survey.md](10-vr-code-survey.md) | REFramework VR internals (extraction notes) and Overlord architecture, from cloned source | 2026-10-07 |
| [11-massfps.md](11-massfps.md) | MassFPS (LE1 first-person camera mod): permissions, how it works, what to keep or change for VR | 2026-10-07 |
| [12-vr-frameworks-survey.md](12-vr-frameworks-survey.md) | Any framework to build on instead? UEVR, VorpX, Geo-11/3Dmigoto, **vrframework** (MIT guide + core) | 2026-10-07 |
| [13-existing-me-vr-mods.md](13-existing-me-vr-mods.md) | **Prior art:** Halcyon's GPL MassEffect1/2/3-VR (working stereo for all three games) and the closed ME2VR (motion controls). What they teach us | 2026-10-07 |
| [14-vrik.md](14-vrik.md) | VRIK: arms and body following the controllers. Why route 1 (SkelControlLimb) is out, why bone queries through ProcessEvent failed, LE1 skeleton layouts, the UpdateSkelPose writeback (route 2), the plan | 2026-10-08 |
| [15-hud.md](15-hud.md) | The HUD in VR (M5): BioSFPanel variables, GFxMovie render-to-texture, the options in order of effort | 2026-10-08 |
| [sources.md](sources.md) | Every URL consulted | 2026-10-07 |

## Conventions

- **Sourced** statements cite a URL from [sources.md](sources.md).
- **[verify]** marks a hypothesis from general UE3/UDK knowledge that hasn't been checked against
  ME LE yet.
- **Not found** results are recorded too, so we don't repeat dead-end searches.
