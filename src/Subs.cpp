#include "Subs.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>

#include <algorithm>

using namespace Subs;

std::optional<SPos> Subs::decode(WORKSPACEID id) {
    if (id <= 0 || id % GROUP_SPAN == 0)
        return std::nullopt;

    return SPos{.group = sc<int>(id % GROUP_SPAN), .sub = sc<int>(id / GROUP_SPAN) + 1};
}

WORKSPACEID Subs::encode(int group, int sub) {
    return sc<WORKSPACEID>(sub - 1) * GROUP_SPAN + group;
}

std::optional<SPos> Subs::posOf(const PHLWORKSPACE& ws) {
    if (!ws || ws->m_isSpecialWorkspace || ws->inert())
        return std::nullopt;

    return decode(ws->m_id);
}

CLayoutMap Subs::layout(const PHLMONITOR& mon) {
    CLayoutMap map;

    for (const auto& ws : State::workspaceState()->workspaces()) {
        if (mon && ws->m_monitor != mon)
            continue;

        const auto POS = posOf(ws.lock());
        if (!POS)
            continue;

        map[POS->group].push_back(POS->sub);
    }

    for (auto& [_, subs] : map) {
        std::ranges::sort(subs);
    }

    return map;
}

bool Subs::groupHasWindows(int group, const PHLMONITOR& mon) {
    for (const auto& ws : State::workspaceState()->workspaces()) {
        if (mon && ws->m_monitor != mon)
            continue;

        const auto POS = posOf(ws.lock());
        if (POS && POS->group == group && ws->getWindowCount() > 0)
            return true;
    }

    return false;
}

PHLWORKSPACE Subs::workspaceFor(int group, int sub) {
    return State::workspaceState()->query().id(encode(group, sub)).run();
}

int CState::lastUsedSub(int group, const CLayoutMap& map) const {
    const auto IT = map.find(group);
    if (IT == map.end() || IT->second.empty())
        return 1;

    if (const auto LAST = m_lastUsed.find(group); LAST != m_lastUsed.end() && std::ranges::contains(IT->second, LAST->second))
        return LAST->second;

    return IT->second.front();
}

int CState::entrySub(int group, bool rowMode, const CLayoutMap& map) const {
    if (rowMode) {
        if (const auto IT = map.find(group); IT != map.end() && std::ranges::contains(IT->second, m_row))
            return m_row;
    }

    return lastUsedSub(group, map);
}

int CState::newSubFor(int group, const CLayoutMap& map) const {
    const auto               IT   = map.find(group);
    const std::vector<int>   NONE = {};
    const std::vector<int>&  subs = IT == map.end() ? NONE : IT->second;

    if (m_rowMode && !std::ranges::contains(subs, m_row))
        return m_row;

    int candidate = 1;
    for (const int s : subs) {
        if (s != candidate)
            break;
        candidate++;
    }

    return candidate;
}

void CState::onWorkspaceActive(const PHLWORKSPACE& ws) {
    const auto POS = posOf(ws);
    if (!POS)
        return;

    m_lastUsed[POS->group] = POS->sub;

    if (!m_keepRow)
        m_row = POS->sub;
}

void CState::seedFromMonitors() {
    for (const auto& mon : State::monitorState()->monitors()) {
        if (const auto POS = posOf(mon->m_activeWorkspace))
            m_lastUsed[POS->group] = POS->sub;
    }

    const auto MON = Desktop::focusState()->monitor();
    if (const auto POS = MON ? posOf(MON->m_activeWorkspace) : std::nullopt)
        m_row = POS->sub;
}
