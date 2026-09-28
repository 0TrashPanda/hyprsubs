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

> Status: v1 implemented, keyboard dispatchers tested; trackpad swipes still need real-hardware testing. Target: Hyprland **v0.56.2** (`efb50993780079460b0cbed1363e2166a2de1d9f`).

## Install

hyprsubs is a compiled plugin: it has to be built against the same Hyprland version you're running, and rebuilt whenever Hyprland updates. It refuses to load if the versions don't match. Tested on Hyprland **v0.56.2**.

Both of Hyprland's config formats are supported. The examples show the **Lua config** (`hyprland.lua`) first, then the hyprlang equivalent (`hyprland.conf`).

### With hyprpm (recommended)

`hyprpm` is Hyprland's plugin manager. It clones, builds, and rebuilds the plugin after Hyprland updates. Some distro packages don't include it (Arch's `hyprland` 0.56 doesn't); there it has to be built from Hyprland's source (`hyprpm/`).

```bash
hyprpm update                                          # fetch headers for your Hyprland version
hyprpm add https://github.com/0TrashPanda/hyprsubs     # clone + build
hyprpm enable hyprsubs
```

To load it at login:

```lua
-- hyprland.lua
hl.on("hyprland.start", function()
    hl.exec_cmd("hyprpm reload -n")
end)
```

```ini
# hyprland.conf
exec-once = hyprpm reload -n
```

After a Hyprland update, run `hyprpm update` to rebuild. It also pulls new hyprsubs commits.

The install also creates `~/.config/hypr/hyprsubs.lua` (or `hyprsubs.conf`) with the default binds, gestures and options, see [Default config](#default-config).

### Manually

Needs `make`, `g++`, `pkg-config` and the Hyprland headers matching your running Hyprland (`pkg-config --modversion hyprland` should print the same version as `hyprctl version`). Keep the clone somewhere stable, since Hyprland loads the `.so` from that path:

```bash
git clone https://github.com/0TrashPanda/hyprsubs ~/.local/src/hyprsubs
cd ~/.local/src/hyprsubs
make
make config     # creates ~/.config/hypr/hyprsubs.lua / .conf, see Default config
```

Load it at startup (absolute path):

```lua
-- hyprland.lua
hl.plugin.load("/home/<you>/.local/src/hyprsubs/hyprsubs.so")
```

```ini
# hyprland.conf
plugin = /home/<you>/.local/src/hyprsubs/hyprsubs.so
```

To update, and after every Hyprland update:

```bash
cd ~/.local/src/hyprsubs && git pull && make
hyprctl plugin unload "$PWD/hyprsubs.so" && hyprctl plugin load "$PWD/hyprsubs.so"
```

### Default config

Both install methods put a default config next to your Hyprland config (`~/.config/hypr`, or under `$XDG_CONFIG_HOME`), in the matching format:

| Your config | Created file | Enable it with (at the **end** of your config) |
|---|---|---|
| `hyprland.lua` | [`hyprsubs.lua`](hyprsubs.lua) | `require("hyprsubs")` |
| `hyprland.conf` | [`hyprsubs.conf`](hyprsubs.conf) | `source = ~/.config/hypr/hyprsubs.conf` |

If both exist, both are created. If neither does, the Lua one is. Each contains:

- all [plugin options](#configuration) at their defaults,
- the vertical and 4-finger gestures,
- the `SUPER (+ SHIFT / CTRL) + 1–9` binds from [Keyboard](#keyboard), each replacing (unbinding) whatever the key did before.

Notes:

- **Created only if it doesn't exist yet,** so later updates never overwrite your edits. To get a fresh copy, delete the file and run `hyprpm update` or `make config` again.
- **Enable it at the end** of your config, after your other binds (and after any generated binds, e.g. DMS's `binds.conf`), so its unbinds replace the stock `SUPER + N` workspace binds.
- **3-finger horizontal gesture:** that line is commented out in the file. Hyprland's default config already has it, and defining a gesture twice is a config error. Uncomment it if your config doesn't have one.
- **Lua:** the file does nothing until the plugin is loaded (it checks `hl.plugin.hyprsubs`). When the plugin loads, it reloads the config once so the file takes effect. That covers both `hl.plugin.load` and hyprpm.

### Then

1. Check the binds and gestures fit your setup: [Keyboard](#keyboard), [Configuration](#configuration), [Migration from a stock config](#migration-from-a-stock-config).
2. Optional: the DMS bar widget, [hyprsubs-dms](https://github.com/0TrashPanda/hyprsubs-dms).

### Development

```bash
make
hyprctl plugin load "$PWD/hyprsubs.so"
```

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

All actions below are exposed as dispatchers (see [Dispatchers](#dispatchers)); the binds are the intended defaults (set up by the [default config](#default-config)).

| Bind | From another group | While already in group N |
|---|---|---|
| `SUPER + N` | Go to group N, on its last-used sub | Go to the next existing sub. After the last sub, wrap to the first. No-op if only one sub. |
| `SUPER + SHIFT + N` | Move the focused window to group N's last-used sub; view follows | Move the focused window to the next existing sub (wrapping); view follows. No-op if only one sub. |
| `SUPER + CTRL + N` | Create a new empty sub at the end of group N and go there | Same |
| `SUPER + CTRL + SHIFT + N` | Create a new sub at the end of group N, move the focused window there; view follows | Same |

Rules:

- Only the `CTRL` binds create subs from the keyboard. Keyboard navigation never creates subs (trackpad swipes can, see [Swiping past the end](#swiping-past-the-end)).
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
- Past the first / last group: see [Swiping past the end](#swiping-past-the-end) (`swipe_group_edge`, default `create`).

### Vertical: switch sub

- Target: the next / previous **existing** sub in the current group (gaps are skipped).
- Natural direction: fingers up → next sub comes in from below. Fingers down → previous sub comes in from above. With `subs_above = true` this flips: higher subs sit above, so fingers down brings in the next sub.
- Past the first / last sub: see [Swiping past the end](#swiping-past-the-end) (`swipe_sub_edge`, default `stop`).

### Tracking and release

All native behavior, reused as-is:

- Both axes track the fingers **1:1**.
- Commit vs snap-back is decided by `gestures:workspace_swipe_cancel_ratio` (distance) and `gestures:workspace_swipe_min_speed_to_force` (flick speed).
- The finish animation (commit or snap-back) stays on the swipe axis.
- On a wrap, the target slides in from the side you are swiping toward. It never animates backwards across the skipped workspaces.

### Swiping past the end

`swipe_group_edge` (horizontal) and `swipe_sub_edge` (vertical) each take one of:

| Value | Past the last group / sub | Past the first |
|---|---|---|
| `stop` | Rubber-bands and snaps back | Same |
| `wrap` | Wraps to the first | Wraps to the last |
| `create` | Creates a new one and lands on it | Rubber-bands |

With `create`:

- **Group:** the new group is the lowest unused group number after the current one. You land on its sub 1, or on the current row in row mode.
- **Sub:** the new sub is the last sub + 1, in the "next" direction (below, or above with `subs_above`).
- **While swiping,** only the current workspace slides away and nothing is shown behind it yet, like Hyprland's `workspace_swipe_create_new`. The new workspace is created when the swipe commits.
- **Empty workspace:** nothing is created when you're on an empty workspace, so you can't chain empty groups or subs. It rubber-bands instead.

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
- **Toggle** (`rowmode` [dispatcher](#dispatchers)): while on, it changes the keyboard:
  - `SUPER + N` from another group goes to group N at the current row.
  - `SUPER + SHIFT + N` from another group moves the window to group N at the current row.
  - `SUPER + N` / `SUPER + SHIFT + N` inside group N still cycle subs, unchanged.
  - `SUPER + CTRL (+ SHIFT) + N` creates the new sub at the current row if that slot is free, otherwise the lowest free sub.
- **3-finger horizontal swipe:** row mode only when the toggle is on **and** `row_mode_3finger = true`.

### Remembered row

- The plugin keeps a **current row**, starting as the sub you are on.
- A row-mode horizontal move targets the current row in the next group. If that group doesn't have it (3.2 missing), you land on the group's last-used sub, and the row is **kept**. The next row-mode move tries the row again (→ 4.2).
- The row is **reset** to your current sub when you change sub on purpose: a vertical swipe, `SUPER + N` cycling, or the `sub` / `movesub` dispatchers.
- Any other workspace change also resets it to the sub you land on (a non-row-mode move, a bar click, a native `workspace` dispatch). Only row-mode horizontal moves keep it.
- Groups with no windows are still skipped.

## Dispatchers

| Action | Argument | Does |
|---|---|---|
| `group` | `N` | Same as `SUPER + N` |
| `movegroup` | `N` | Same as `SUPER + SHIFT + N` |
| `newsub` | `N` | Same as `SUPER + CTRL + N` |
| `movenewsub` | `N` | Same as `SUPER + CTRL + SHIFT + N` |
| `sub` | `next` \| `prev` \| `S` | Go to the next / previous existing sub (wrapping), or to sub `S` of the current group |
| `movesub` | `next` \| `prev` \| `S` | Move the focused window there; view follows |
| `groupcycle` | `next` \| `prev` | Go to the next / previous group with windows (wrapping), on its last-used sub |
| `rowmode` | `on` \| `off` \| `toggle` | Row mode toggle (Lua: no argument = `toggle`) |

### Lua config

Each action is `hl.plugin.hyprsubs.<action>(arg)`. Like `hl.dsp.*`, calling it returns a dispatcher that you bind or dispatch:

```lua
local subs = hl.plugin.hyprsubs

hl.bind("SUPER + 1", subs.group(1))
hl.bind("SUPER + Page_Down", subs.groupcycle("next"))
hl.bind("SUPER + R", subs.rowmode())

hl.dispatch(subs.sub("next"))
```

From a shell (with a Lua config, `hyprctl dispatch` takes Lua):

```bash
hyprctl dispatch 'hl.plugin.hyprsubs.group(2)'
```

Also available:

- `hl.plugin.hyprsubs.run(action, arg)` runs an action immediately and returns `{ ok = bool, error = string? }`.
- `hl.plugin.hyprsubs.state()` returns the [state JSON](#state-query) as a string.

`hl.plugin.hyprsubs` only exists once the plugin is loaded, so guard code that uses it with `if hl.plugin.hyprsubs then … end`. The plugin reloads the config after loading, so guarded code still runs.

### hyprlang config

Each action is the dispatcher `hyprsubs:<action>`:

```ini
bind = SUPER, 1, hyprsubs:group, 1
bind = SUPER, Page_Down, hyprsubs:groupcycle, next
```

```bash
hyprctl dispatch hyprsubs:group 2
```

## Autostart

Nothing plugin-specific: target the workspace ID directly. Window rules (they also catch relaunches):

```lua
-- hyprland.lua
hl.window_rule({ match = { class = "^(discord)$" }, workspace = "3 silent" })   -- 3.1
hl.window_rule({ match = { class = "^(Element)$" }, workspace = "103 silent" }) -- 3.2
hl.window_rule({ match = { class = "^(code)$" },    workspace = "102 silent" }) -- 2.2
```

```ini
# hyprland.conf
windowrule = workspace 3 silent, match:class ^(discord)$     # 3.1
windowrule = workspace 103 silent, match:class ^(Element)$   # 3.2
windowrule = workspace 102 silent, match:class ^(code)$      # 2.2
```

## State query

```bash
hyprctl hyprsubs -j
```

Returns the current position and every group's subs, for scripts and bars (with a Lua config also `hl.plugin.hyprsubs.state()`):

```json
{
  "current": { "group": 2, "sub": 3, "workspace": 202 },
  "row_mode": false,
  "row": 3,
  "groups": [
    { "group": 1, "subs": [1],    "total": 1, "last_used": 1, "windows": 2 },
    { "group": 2, "subs": [1, 3], "total": 2, "last_used": 3, "windows": 3 },
    { "group": 3, "subs": [1, 2], "total": 2, "last_used": 1, "windows": 1 }
  ]
}
```

- `subs`: existing sub numbers in ascending order (gaps visible).
- `total`: number of existing subs.
- `last_used`: the sub `SUPER + N` / horizontal swipe would land on (outside row mode).
- `windows`: number of windows across the group's subs.
- `row_mode`: toggle state.
- `row`: the remembered row, i.e. the project you're in, even if you've landed on a different sub.

### Change event

Whenever this state changes, the plugin posts the same JSON on one line to Hyprland's event socket (`.socket2.sock`):

```
hyprsubs>>{"current": { "group": 2, "sub": 3, "workspace": 202 },"row_mode": false, ...}
```

Changes are collected until the compositor is idle and duplicates are dropped, so a burst of workspace or window events produces one line. Bars can listen to this instead of polling `hyprctl`. The DMS bar widget lives in its own repo, [hyprsubs-dms](https://github.com/0TrashPanda/hyprsubs-dms).

## Configuration

Swipe feel is configured with Hyprland's native options:

```lua
-- hyprland.lua
hl.gesture({ fingers = 3, direction = "horizontal", action = "workspace" })
hl.gesture({ fingers = 3, direction = "vertical",   action = "workspace" })
hl.gesture({ fingers = 4, direction = "horizontal", action = "workspace" }) -- row mode swipe

hl.config({
    gestures = {
        workspace_swipe_cancel_ratio       = 0.5,
        workspace_swipe_min_speed_to_force = 30,
        workspace_swipe_distance           = 300,
    },
})
```

```ini
# hyprland.conf
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

```lua
-- hyprland.lua (after the plugin is loaded, see Default config)
hl.config({
    plugin = {
        hyprsubs = {
            row_mode                = false,    -- toggle state at startup
            row_mode_3finger        = false,    -- 3-finger horizontal also uses row mode while the toggle is on
            swipe_group_edge        = "create", -- past the first / last group: "stop", "wrap" or "create"
            swipe_sub_edge          = "stop",   -- past the first / last sub: "stop", "wrap" or "create"
            subs_above              = false,    -- higher subs sit above the current one instead of below
            vertical_swipe_distance = 0,        -- like workspace_swipe_distance, vertical swipes only (0 = use that)
        },
    },
})
```

```ini
# hyprland.conf
plugin {
    hyprsubs {
        row_mode = false            # toggle state at startup
        row_mode_3finger = false    # 3-finger horizontal also uses row mode while the toggle is on
        swipe_group_edge = create   # past the first / last group: stop, wrap or create
        swipe_sub_edge = stop       # past the first / last sub: stop, wrap or create
        subs_above = false          # higher subs sit above the current one instead of below
        vertical_swipe_distance = 0 # like workspace_swipe_distance, for vertical swipes only (0 = use that)
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

- `left`: from the plugin's move (next group / sub → `true`, previous → `false`; `true` slides the new workspace in from the right / bottom). Because the wraparound check is folded into `ANIMTOLEFT` before the call, this overrides it too: a jump like 2.2 (102) → 3.1 (3) slides right even though the ID goes down.
- `style`: `slide` for group changes, `slidevert` for sub changes. Passing it as the argument (instead of writing `m_animationStyle`) leaves the workspace's own config untouched for non-plugin switches.

A switch counts as the plugin's own from the moment one of its dispatchers calls `changeWorkspace` until it returns; `startAnimation` calls outside that window pass through unchanged.

### Maintenance

The copied engine has to be re-synced when upstream changes `UnifiedWorkspaceSwipeGesture.cpp` (it already differs between v0.56.2 and current main). Everything else is a small set of hooks.

## Not in v1

- Multi-monitor awareness (per-monitor groups). v1 acts on the focused monitor only.
- Overview / grid view of all groups and subs.
- Dragging windows between subs with the trackpad.

## Migration from a stock config

The [default config](#default-config) does steps 1 and 2 for you.

1. Add the vertical (and optionally 4-finger) gesture next to the stock 3-finger horizontal one, see [Configuration](#configuration).
2. Replace the `SUPER + N` / `SUPER + SHIFT + N` workspace binds with hyprsubs [dispatchers](#dispatchers) (DMS's `binds.conf` may define these).
3. Existing window rules keep working: workspace `N` is group N's first sub. Only rules for extra subs need new IDs (`N.2 → 100 + N`).
4. Moving from `hyprland.conf` to `hyprland.lua`: `hyprsubs:<action> arg` becomes `hl.plugin.hyprsubs.<action>(arg)`, `plugin { hyprsubs { … } }` becomes `hl.config({ plugin = { hyprsubs = { … } } })`, and `source = …/hyprsubs.conf` becomes `require("hyprsubs")` with the Lua template (`make config` installs it once `hyprland.lua` exists).
