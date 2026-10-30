---
type: Gotcha
title: Never write Shepard's Camera or God bones
description: LE1's camera hangs off the `Camera` and `God` bones through SpaceBases, so a body pose that writes them moves the view with the body.
tags: [le1, vrik, bones, camera]
status: stable
timestamp: 2026-10-30T19:53:43Z
generated: { by: claude-code/fable-5.1, at: 2026-10-30T19:53:43Z }
bitten: [2026-10-21]
---

# Symptom

The view follows the body: a lean, body follow or a whole-body move drags the camera with it.

# Cause

LE1's camera reads the `Camera` and `God` bones' SpaceBases. Any pose write that includes them moves the view.

# Fix in place

`engine/BonePose` skips them (`Request::keep`); arm IK logs once per skeleton whether a written subtree contains them,
and the heartbeat if a solve moved one. Any whole-body move (body follow's `Root` subtree) excludes them.

# Citations

[1] [research/14, "Camera hook bones: never written" (2026-10-21)](../../../research/14-vrik.md).
[2] [research/18, status 2026-10-21](../../../research/18-camera-and-hands.md).
