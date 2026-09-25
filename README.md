# hyprsubs

A Hyprland plugin that turns flat workspaces into a 2D layout: **groups** (horizontal) that each contain one or more **subs** (vertical), with 1:1 trackpad swiping on both axes.

```
          group 1      group 2      group 3      group 4
          ───────      ───────      ───────      ───────
sub 1  │  browser      terminals    discord      music
sub 2  │  browser      vscode       matrix
sub 3  │                            ...
```

Horizontal movement switches groups. Vertical movement switches subs inside the current group.

> Status: spec only. Nothing implemented yet. Target: Hyprland **v0.56.2** (`efb50993780079460b0cbed1363e2166a2de1d9f`).

---

## Concepts

| Term | Meaning |
|---|---|
| **Group** | A horizontal slot, addressed by number (1–99). The number row keys map to groups 1–9; 10–99 are reachable via dispatchers. |
| **Sub** | A vertical layer inside a group (2.1, 2.2, …). Uncapped. |
| **Last-used sub** | Per group, the sub you were most recently on. Used whenever you enter a group from outside. If a group has no history yet (e.g. right after login), its lowest existing sub is used. |
| **Row** | All subs with the same index across groups (2.2, 3.2, 4.2, …). Used by [row mode](#row-mode). |

## Workspace model

- Every (group, sub) pair is an ordinary Hyprland workspace with a fixed ID:

  ```
  id = (sub − 1) × 100 + group
  ```

  The last two digits are the group, everything before them is the sub minus one.

  | Group.Sub | Workspace ID |
  |---|---|
  | 1.1 | 1 |
  | 2.1 | 2 |
  | 2.2 | 102 |
  | 3.14 | 1303 |
  | 42.7 | 642 |

- **The first sub of every group is a plain workspace ID (1–99).** Existing window rules and binds keep working, and with the plugin unloaded (or crashed) you are left with a normal Hyprland setup where only the extra subs sit on higher IDs.
- IDs are not ordered along the horizontal axis (2.2 = 102 → 3.1 = 3 goes right but down in ID). This is why the plugin uses its own copy of the swipe engine (see [Implementation approach](#implementation-approach)).
- Because they are plain workspaces, window rules, `movetoworkspace`, etc. keep working using these IDs (e.g. `workspace 3` for Discord on 3.1).
- Any workspace whose ID fits the scheme is treated as a sub, no matter how it was created (window rule, `exec-once`, dispatcher).
- IDs that don't fit are ignored by the plugin: multiples of 100 (100, 200, … would decode to group 0), named workspaces and special workspaces. They still work as normal Hyprland workspaces but are never part of group / sub navigation.
- Sub numbers are **not renumbered**. If 2.2 disappears while 2.1 and 2.3 exist, 2.3 stays 2.3 and navigation skips the gap.
- Empty subs are destroyed when you leave them (normal Hyprland behavior). The sub you are currently on is never destroyed.
- The plugin tracks the **last-used sub per group**.
- Only the **focused monitor** is affected.

## Keyboard

All actions below are exposed as dispatchers (see [Dispatchers](#dispatchers)); the binds are the intended defaults.

| Bind | From another group | While already in group N |
|---|---|---|
| `SUPER + N` | Go to group N, on its last-used sub | Go to the next existing sub. After the last sub, wrap to the first. No-op if only one sub. |
| `SUPER + SHIFT + N` | Move the focused window to group N's last-used sub; view follows | Move the focused window to the next existing sub (wrapping); view follows. No-op if only one sub. |
| `SUPER + CTRL + N` | Create a new empty sub at the end of group N and go there | Same |
| `SUPER + CTRL + SHIFT + N` | Create a new sub at the end of group N, move the focused window there; view follows | Same |

Rules:

- Only the `CTRL` binds create subs. Navigation never creates subs.
- New subs fill the **lowest free sub number** in group N (with 2.1 and 2.3, the new sub is 2.2). If group N has no subs, the new sub is `N.1`. In [row mode](#row-mode), the current row is used instead if that slot is free.
- Entering a group that has no existing subs (via `SUPER + N`) goes to `N.1`.
- Keyboard-triggered switches animate on the matching axis: group changes slide horizontally, sub changes slide vertically.

## Trackpad

3-finger swipe on both axes, using a copy of Hyprland's native workspace swipe engine (same tracking, thresholds and feel; see [Implementation approach](#implementation-approach)).

### Axis lock

Handled natively by Hyprland: the first ~5px of movement decides horizontal or vertical, and the gesture stays on that axis until the fingers lift.

### Horizontal: switch group

- Target: the next / previous group **that has at least one window**. Empty groups are skipped, like the native swipe.
- Lands on that group's last-used sub (or the same sub index in [row mode](#row-mode)).
- Past the first / last group: wraps around if `swipe_wrap = true`, otherwise rubber-bands and snaps back.

### Vertical: switch sub

- Target: the next / previous **existing** sub in the current group (gaps are skipped).
- Natural direction: fingers up → next sub comes in from below. Fingers down → previous sub comes in from above.
- Past the first / last sub: wraps around if `swipe_wrap = true`, otherwise rubber-bands and snaps back.

### Tracking and release

All native behavior, reused as-is:

- Both axes track the fingers **1:1**.
- Commit vs snap-back is decided by `gestures:workspace_swipe_cancel_ratio` (distance) and `gestures:workspace_swipe_min_speed_to_force` (flick speed).
- The finish animation (commit or snap-back) stays on the swipe axis.
- On a wrap, the target slides in from the side you are swiping toward. It never animates backwards across the skipped workspaces.

## Row mode

An alternative way to use subs: each sub index is a **project**, and groups are the same roles across projects.

```
             browser   terminals   chat
project 1 │  1.1       2.1         3.1
project 2 │  1.2       2.2         3.2
```

In row mode, horizontal moves keep the **same sub index** instead of using last-used: 2.2 → 3.2.

### Triggers

- **4-finger horizontal swipe:** always row mode.
- **Toggle** (`hyprsubs:rowmode`): while on, it changes the keyboard:
  - `SUPER + N` from another group goes to group N at the current row.
  - `SUPER + SHIFT + N` from another group moves the window to group N at the current row.
  - `SUPER + N` / `SUPER + SHIFT + N` inside group N still cycle subs, unchanged.
  - `SUPER + CTRL (+ SHIFT) + N` creates the new sub at the current row if that slot is free, otherwise the lowest free sub.
- **3-finger horizontal swipe:** row mode only when the toggle is on **and** `row_mode_3finger = true`.

### Remembered row

- The plugin keeps a **current row**, starting as the sub you are on.
- A row-mode horizontal move targets the current row in the next group. If that group doesn't have it (3.2 missing), you land on the group's last-used sub, and the row is **kept**. The next row-mode move tries the row again (→ 4.2).
- The row is **reset** to your current sub when you change sub on purpose: a vertical swipe, `SUPER + N` cycling, or `hyprsubs:sub` / `hyprsubs:movesub`.
- Groups with no windows are still skipped.

## Dispatchers

| Dispatcher | Argument | Action |
|---|---|---|
| `hyprsubs:group` | `N` | Same as `SUPER + N` |
| `hyprsubs:movegroup` | `N` | Same as `SUPER + SHIFT + N` |
| `hyprsubs:newsub` | `N` | Same as `SUPER + CTRL + N` |
| `hyprsubs:movenewsub` | `N` | Same as `SUPER + CTRL + SHIFT + N` |
| `hyprsubs:sub` | `next` \| `prev` \| `S` | Go to the next / previous existing sub (wrapping), or to sub `S` of the current group |
| `hyprsubs:movesub` | `next` \| `prev` \| `S` | Move the focused window there; view follows |
| `hyprsubs:groupcycle` | `next` \| `prev` | Go to the next / previous group with windows (wrapping), on its last-used sub |
| `hyprsubs:rowmode` | `on` \| `off` \| `toggle` | Row mode toggle |

Example binds (`hyprland.conf`):

```ini
bind = SUPER, 1, hyprsubs:group, 1
bind = SUPER SHIFT, 1, hyprsubs:movegroup, 1
bind = SUPER CTRL, 1, hyprsubs:newsub, 1
bind = SUPER CTRL SHIFT, 1, hyprsubs:movenewsub, 1
# ...repeat for 2–9
```

## Autostart

Nothing plugin-specific: target the workspace ID directly.

Window rules (preferred, also catch relaunches):

```ini
windowrule = workspace 3 silent, match:class ^(discord)$     # 3.1
windowrule = workspace 103 silent, match:class ^(Element)$   # 3.2
windowrule = workspace 102 silent, match:class ^(code)$      # 2.2
```

One-off at launch:

```ini
exec-once = [workspace 2 silent] kitty                       # 2.1
```

## State query

```bash
hyprctl hyprsubs -j
```

Returns the current position and every group's subs, for scripts and a future DMS integration:

```json
{
  "current": { "group": 2, "sub": 3, "workspace": 202 },
  "row_mode": false,
  "row": 3,
  "groups": [
    { "group": 1, "subs": [1],    "total": 1, "last_used": 1 },
    { "group": 2, "subs": [1, 3], "total": 2, "last_used": 3 },
    { "group": 3, "subs": [1, 2], "total": 2, "last_used": 1 }
  ]
}
```

- `subs`: existing sub numbers in ascending order (gaps visible).
- `total`: number of existing subs.
- `last_used`: the sub `SUPER + N` / horizontal swipe would land on (outside row mode).
- `row_mode`: toggle state.
- `row`: the remembered row, i.e. the project you're in, even if you've landed on a different sub.

## Configuration

Swipe feel is configured with Hyprland's native options:

```ini
gesture = 3, horizontal, workspace
gesture = 3, vertical, workspace
gesture = 4, horizontal, workspace   # row mode swipe

gestures {
    workspace_swipe_cancel_ratio = 0.5
    workspace_swipe_min_speed_to_force = 30
    workspace_swipe_distance = 300
}
```

Plugin options (only what the native engine doesn't cover):

```ini
plugin {
    hyprsubs {
        row_mode = false            # toggle state at startup
        row_mode_3finger = false    # 3-finger horizontal also uses row mode while the toggle is on
        swipe_wrap = false          # trackpad wraps past the first / last group and sub
    }
}
```

## Implementation approach

Checked against the v0.56.2 source.

### Trackpad: copied swipe engine

The native engine lives in `src/managers/input/UnifiedWorkspaceSwipeGesture.cpp` (~360 lines). The plugin ships a copy of it and hooks `CWorkspaceSwipeGesture::begin / update / end` (`src/managers/input/trackpad/gestures/WorkspaceSwipeGesture.cpp`) to drive the copy instead of the global `g_pUnifiedWorkspaceSwipe`. Tracking, commit ratio, flick speed and snap-back math stay verbatim. Three changes in the copy:

1. **Targets.** The native code gets neighbors from `getWorkspaceIDNameFromString("m-1" / "m+1")`. The copy asks the plugin instead (neighbor group or neighbor sub, row mode or not).
2. **ID order checks removed.** The native code refuses a target that is on the numerically wrong side (left must be a lower ID, right a higher one). With `(sub − 1) × 100 + group` that doesn't hold horizontally, so those checks go.
3. **Axis from the gesture.** The native code decides horizontal vs vertical drawing from the workspace's animation style (`slidevert`). The copy uses the gesture direction passed to `begin()` instead.

`begin()` also receives the swipe event, which carries the finger count: 4 fingers → row mode.

### Keyboard: animation direction

`CMonitor::changeWorkspace` (`src/output/Monitor.cpp`) computes the slide direction once, as `ANIMTOLEFT = shouldWraparound(new, old) ^ (new > old)`, and takes the style from the target's `m_animationStyle`. Both are then passed to `Animation::Workspace::startAnimation` for the old workspace (`OUT`) and the new one (`IN`).

The plugin hooks `startAnimation` and, while one of its own switches is running, replaces both arguments:

- `left`: from the plugin's move (previous group / sub → `true`, next → `false`). Because the wraparound check is folded into `ANIMTOLEFT` before the call, this overrides it too: a jump like 2.2 (102) → 3.1 (3) slides right even though the ID goes down.
- `style`: `slide` for group changes, `slidevert` for sub changes. Passing it as the argument (instead of writing `m_animationStyle`) leaves the workspace's own config untouched for non-plugin switches.

A switch counts as the plugin's own from the moment a `hyprsubs:*` dispatcher calls `changeWorkspace` until it returns; `startAnimation` calls outside that window pass through unchanged.

### Maintenance

The copied engine has to be re-synced when upstream changes `UnifiedWorkspaceSwipeGesture.cpp` (it already differs between v0.56.2 and current main). Everything else is a small set of hooks.

## Not in v1

- Multi-monitor awareness (per-monitor groups). v1 acts on the focused monitor only.
- DMS bar integration. DMS currently draws one shape per workspace, so each sub shows as its own shape. A later DMS tweak could read `hyprctl hyprsubs -j` to show sub count / current sub inside the group's shape.
- Overview / grid view of all groups and subs.
- Dragging windows between subs with the trackpad.

## Migration from a stock config

1. Replace `gesture = 3, horizontal, workspace` with the horizontal + vertical pair from [Configuration](#configuration).
2. Replace the `SUPER + N` / `SUPER + SHIFT + N` workspace binds with the `hyprsubs:*` dispatchers above (DMS's `binds.conf` may define these).
3. Existing window rules keep working: workspace `N` is group N's first sub. Only rules for extra subs need new IDs (`N.2 → 100 + N`).
