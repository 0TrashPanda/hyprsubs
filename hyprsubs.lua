-- hyprsubs defaults (Lua config)
--
-- Created once by the hyprsubs install (hyprpm or `make config`); updates never overwrite it,
-- so edit freely. Enable it by adding this line to the END of hyprland.lua (after your other
-- binds, so the unbinds below take effect):
--
--   require("hyprsubs")
--
-- Workspace IDs: id = (sub - 1) * 100 + group, so group N's first sub is plain workspace N.

local subs = hl.plugin.hyprsubs

-- Not loaded yet: the plugin reloads the config once it is, and this file runs again.
if not subs then
    return
end

hl.config({
    plugin = {
        hyprsubs = {
            row_mode                = false,    -- row-mode toggle state at startup
            row_mode_3finger        = false,    -- 3-finger horizontal swipe also uses row mode while the toggle is on
            swipe_group_edge        = "create", -- past the first / last group: "stop", "wrap" or "create"
            swipe_sub_edge          = "stop",   -- past the first / last sub: "stop", "wrap" or "create"
            subs_above              = false,    -- higher subs sit above the current one instead of below
            vertical_swipe_distance = 0,        -- like gestures.workspace_swipe_distance, vertical only (0 = use that)
        },
    },
})

-- Trackpad: 3 fingers horizontal = groups, vertical = subs, 4 fingers horizontal = row mode.
-- Hyprland's default config already has the 3-finger horizontal gesture (defining it twice is a
-- config error), so it's commented out here. Uncomment it if your hyprland.lua doesn't have it.
-- hl.gesture({ fingers = 3, direction = "horizontal", action = "workspace" })
hl.gesture({ fingers = 3, direction = "vertical",   action = "workspace" })
hl.gesture({ fingers = 4, direction = "horizontal", action = "workspace" })

-- Same swipes with SUPER held: the focused window comes along to the target group / sub.
-- (Any workspace swipe started with a modifier held does this; pick your own mods.)
hl.gesture({ fingers = 3, direction = "horizontal", action = "workspace", mods = "SUPER" })
hl.gesture({ fingers = 3, direction = "vertical",   action = "workspace", mods = "SUPER" })
hl.gesture({ fingers = 4, direction = "horizontal", action = "workspace", mods = "SUPER" })

-- SUPER + N                 go to group N (last-used sub), or cycle its subs if already there
-- SUPER + SHIFT + N         move the focused window there
-- SUPER + CTRL + N          new sub in group N
-- SUPER + CTRL + SHIFT + N  new sub in group N, move the focused window there
local mainMod = "SUPER"

for i = 1, 9 do
    local key = tostring(i)
    local binds = {
        [mainMod .. " + " .. key]                  = subs.group(i),
        [mainMod .. " + SHIFT + " .. key]          = subs.movegroup(i),
        [mainMod .. " + CTRL + " .. key]           = subs.newsub(i),
        [mainMod .. " + CTRL + SHIFT + " .. key]   = subs.movenewsub(i),
    }

    for keys, dispatcher in pairs(binds) do
        hl.unbind(keys) -- replace the stock workspace binds
        hl.bind(keys, dispatcher)
    end
end
