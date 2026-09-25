#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>

namespace Subs::Cfg {
    inline SP<Config::Values::CBoolValue> rowMode;
    inline SP<Config::Values::CBoolValue> rowMode3Finger;
    inline SP<Config::Values::CBoolValue> wrap;
    inline SP<Config::Values::CBoolValue> above;
    inline SP<Config::Values::CIntValue>  verticalDistance;

    void                                  registerValues(HANDLE handle);

    inline bool                           rowModeAtStart() {
        return rowMode && rowMode->value();
    }

    inline bool rowModeFor3Finger() {
        return rowMode3Finger && rowMode3Finger->value();
    }

    inline bool swipeWrap() {
        return wrap && wrap->value();
    }

    // higher subs sit above the current one instead of below
    inline bool subsAbove() {
        return above && above->value();
    }

    // 0 = use gestures:workspace_swipe_distance
    inline int64_t verticalSwipeDistance() {
        return verticalDistance ? verticalDistance->value() : 0;
    }
}
