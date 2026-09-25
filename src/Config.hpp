#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>

namespace Subs::Cfg {
    inline SP<Config::Values::CBoolValue> rowMode;
    inline SP<Config::Values::CBoolValue> rowMode3Finger;
    inline SP<Config::Values::CStringValue> groupEdgeValue;
    inline SP<Config::Values::CStringValue> subEdgeValue;
    inline SP<Config::Values::CBoolValue> above;
    inline SP<Config::Values::CIntValue>  verticalDistance;

    void                                  registerValues(HANDLE handle);

    inline bool                           rowModeAtStart() {
        return rowMode && rowMode->value();
    }

    inline bool rowModeFor3Finger() {
        return rowMode3Finger && rowMode3Finger->value();
    }

    // what a swipe does past the first / last group or sub
    enum eEdge : uint8_t {
        EDGE_STOP = 0, // rubber-band back
        EDGE_WRAP,     // go around to the other end
        EDGE_CREATE,   // past the last one: create a new one (the first end stops)
    };

    inline eEdge parseEdge(const SP<Config::Values::CStringValue>& v) {
        const auto S = v ? v->value() : std::string{};
        if (S == "wrap")
            return EDGE_WRAP;
        if (S == "create")
            return EDGE_CREATE;
        return EDGE_STOP;
    }

    inline eEdge groupEdge() {
        return parseEdge(groupEdgeValue);
    }

    inline eEdge subEdge() {
        return parseEdge(subEdgeValue);
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
