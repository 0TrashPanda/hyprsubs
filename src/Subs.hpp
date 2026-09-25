#pragma once

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Subs {
    // id = (sub - 1) * 100 + group
    constexpr WORKSPACEID GROUP_SPAN = 100;
    constexpr int         MAX_GROUP  = 99;

    struct SPos {
        int  group = 0;
        int  sub   = 0;

        bool operator==(const SPos&) const = default;
    };

    std::optional<SPos> decode(WORKSPACEID id);
    WORKSPACEID         encode(int group, int sub);
    std::optional<SPos> posOf(const PHLWORKSPACE& ws);

    enum eAxis : uint8_t {
        AXIS_GROUP = 0, // horizontal
        AXIS_SUB,       // vertical
    };

    // group -> ascending existing subs
    using CLayoutMap = std::map<int, std::vector<int>>;

    // All existing sub workspaces. If mon is set, only the ones on that monitor.
    CLayoutMap   layout(const PHLMONITOR& mon = nullptr);
    bool         groupHasWindows(int group, const PHLMONITOR& mon = nullptr);
    PHLWORKSPACE workspaceFor(int group, int sub);

    class CState {
      public:
        // last-used sub of a group, or its lowest existing sub, or 1 if none exist
        int  lastUsedSub(int group, const CLayoutMap& map) const;
        // where a horizontal move into `group` lands
        int  entrySub(int group, bool rowMode, const CLayoutMap& map) const;
        // lowest free sub in group, or the current row in row mode if free
        int  newSubFor(int group, const CLayoutMap& map) const;

        void onWorkspaceActive(const PHLWORKSPACE& ws);
        void seedFromMonitors();

        bool                         m_rowMode = false;
        int                          m_row     = 1;

        // set while a row-mode horizontal move runs: the row is kept instead of reset
        bool                         m_keepRow = false;

        std::unordered_map<int, int> m_lastUsed;
    };

    inline CState g_state;

    // Plugin-initiated switch in progress: startAnimation is overridden while set.
    struct SPendingAnim {
        bool  active  = false;
        eAxis axis    = AXIS_GROUP;
        bool  forward = true;
    };

    inline SPendingAnim g_pendingAnim;
}
