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

> Status: spec only. Nothing implemented yet. Target Hyprland version to be pinned before implementation.

---

## Concepts

| Term | Meaning |
|---|---|
| **Group** | A horizontal slot, addressed by number (1, 2, 3, …). The number row keys map to groups 1–9; higher groups are reachable via dispatchers. Groups are uncapped. |
| **Sub** | A vertical layer inside a group (2.1, 2.2, …). Up to 99 per group. |
| **Last-used sub** | Per group, the sub you were most recently on. Used whenever you enter a group from outside. If a group has no history yet (e.g. right after login), its lowest existing sub is used. |
| **Row** | All subs with the same index across groups (2.2, 3.2, 4.2, …). Used by [row mode](#row-mode). |

## Workspace model

- Every (group, sub) pair is an ordinary Hyprland workspace with a fixed ID:

  ```
  id = group × 100 + sub
  ```

  | Group.Sub | Workspace ID |
  |---|---|
  | 1.1 | 101 |
  | 2.1 | 201 |
  | 2.2 | 202 |
  | 3.14 | 314 |

- This ordering matters: moving right always means a higher ID, moving down always means a higher ID. The native swipe engine relies on that (see [Implementation approach](#implementation-approach)).
- Because they are plain workspaces, window rules, `movetoworkspace`, etc. keep working using these IDs (e.g. `workspace 301` for Discord on 3.1).
- Any workspace whose ID fits the scheme is treated as a sub, no matter how it was created (window rule, `exec-once`, dispatcher).
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
- Keyboard wrapping is fine: it does not go through the swipe engine.

## Trackpad

3-finger swipe on both axes, using Hyprland's native workspace swipe engine.

### Axis lock

Handled natively by Hyprland: the first ~5px of movement decides horizontal or vertical, and the gesture stays on that axis until the fingers lift.

### Horizontal: switch group

- Target: the next / previous group **that has at least one window**. Empty groups are skipped, like the native swipe.
- Lands on that group's last-used sub (or the same sub index in [row mode](#row-mode)).
- **No wrapping.** Past the first / last group, the swipe rubber-bands and snaps back.

### Vertical: switch sub

- Target: the next / previous **existing** sub in the current group (gaps are skipped).
- Natural direction: fingers up → next sub comes in from below. Fingers down → previous sub comes in from above.
- **No wrapping.** Past the first / last sub, the swipe rubber-bands and snaps back.

### Tracking and release

All native behavior, reused as-is:

- Both axes track the fingers **1:1**.
- Commit vs snap-back is decided by `gestures:workspace_swipe_cancel_ratio` (distance) and `gestures:workspace_swipe_min_speed_to_force` (flick speed).
- The finish animation (commit or snap-back) stays on the swipe axis.

## Row mode

An alternative way to use subs: each sub index is a **project**, and groups are the same roles across projects.

```
             browser   terminals   chat
project 1 │  1.1       2.1         3.1
project 2 │  1.2       2.2         3.2
```

In row mode, horizontal moves keep the **same sub index** instead of using last-used: 2.2 → 3.2. This is compatible with the native engine, since 202 → 302 is a higher ID.

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
windowrule = workspace 301 silent, match:class ^(discord)$
windowrule = workspace 302 silent, match:class ^(Element)$
windowrule = workspace 202 silent, match:class ^(code)$
```

One-off at launch:

```ini
exec-once = [workspace 201 silent] kitty
```

## State query

```bash
hyprctl hyprsubs -j
```

Returns the current position and every group's subs, for scripts and a future DMS integration:

```json
{
  "current": { "group": 2, "sub": 3, "workspace": 203 },
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
    }
}
```

## Implementation approach

The plugin reuses Hyprland's native swipe engine (`CUnifiedWorkspaceSwipeGesture`) instead of reimplementing tracking, thresholds and flick handling. It hooks three things:

1. **Target selection.** The engine asks the workspace resolver for `m-1` / `m+1`. While a swipe is running, the plugin hooks that resolver call and returns its own target (neighbor group or neighbor sub).
2. **Swipe axis.** The engine decides horizontal vs vertical drawing from the workspace's animation style (`slide` vs `slidevert`), not from the gesture. The plugin hooks the gesture's `begin()` (which knows the direction) and sets a per-workspace animation config (`setConfig`) with the matching style. The same trick, via the style override on the workspace animation, gives keyboard switches the right axis.
3. **ID ordering.** The engine rejects targets on the numerically wrong side (left must be a lower ID, right a higher one). The `group × 100 + sub` scheme satisfies this on both axes. The only thing it rules out is wrapping, which is why the trackpad doesn't wrap.

Fallback if the hooks turn out too fragile: copy the swipe engine file into the plugin and redirect calls to the copy.

## Not in v1

- Multi-monitor awareness (per-monitor groups). v1 acts on the focused monitor only.
- DMS bar integration. DMS currently draws one shape per workspace, so each sub shows as its own shape. A later DMS tweak could read `hyprctl hyprsubs -j` to show sub count / current sub inside the group's shape.
- Overview / grid view of all groups and subs.
- Dragging windows between subs with the trackpad.
- Wrapping on the trackpad.

## Migration from a stock config

1. Replace `gesture = 3, horizontal, workspace` with the horizontal + vertical pair from [Configuration](#configuration).
2. Replace the `SUPER + N` / `SUPER + SHIFT + N` workspace binds with the `hyprsubs:*` dispatchers above (DMS's `binds.conf` may define these).
3. Update window rules that target workspace IDs: workspace `N` becomes `N × 100 + 1` (`1 → 101`, `3 → 301`, etc.).
