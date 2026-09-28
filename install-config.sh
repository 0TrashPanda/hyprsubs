#!/bin/sh
# Installs the default hyprsubs config on first install, matching the Hyprland config in use:
# hyprsubs.lua next to hyprland.lua, hyprsubs.conf next to hyprland.conf (both if both exist,
# the Lua one if neither does). Never overwrites an existing file.
src="$(dirname "$0")"
dir="${XDG_CONFIG_HOME:-$HOME/.config}/hypr"

install_one() { # <file> <enable line>
    dest="$dir/$1"
    if [ -e "$dest" ]; then
        echo "hyprsubs: $dest exists, leaving it alone"
        return 0
    fi
    mkdir -p "$dir" && cp "$src/$1" "$dest" &&
        echo "hyprsubs: installed $dest (enable it with '$2' at the end of your Hyprland config)"
}

found=
if [ -e "$dir/hyprland.lua" ]; then
    install_one hyprsubs.lua 'require("hyprsubs")'
    found=1
fi
if [ -e "$dir/hyprland.conf" ]; then
    install_one hyprsubs.conf "source = $dir/hyprsubs.conf"
    found=1
fi
[ -n "$found" ] || install_one hyprsubs.lua 'require("hyprsubs")'
