#include "SwipeEngine.hpp"
#include "Config.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/event/EventBus.hpp>

#include <algorithm>

using namespace Subs;

static PHLWORKSPACE workspaceByID(WORKSPACEID id) {
    if (id == WORKSPACE_INVALID)
        return nullptr;
    return State::workspaceState()->query().numbered(Workspace::SWorkspaceNumberedID{id}).run();
}

bool CSubSwipe::isGestureInProgress() {
    return !!m_workspaceBegin;
}

bool CSubSwipe::validOrigin() const {
    const auto MONITOR = m_monitor.lock();
    return MONITOR && MONITOR->m_enabled && m_workspaceBegin && m_workspaceBegin->m_monitor == MONITOR && MONITOR->m_activeWorkspace == m_workspaceBegin;
}

void CSubSwipe::resetListeners() {
    m_listeners.monitorDisconnect.reset();
    m_listeners.monitorDestroy.reset();
    m_listeners.workspaceMonitorChanged.reset();
    m_listeners.workspaceActiveChanged.reset();
}

CSubSwipe::SParticipant& CSubSwipe::acquire(PHLWORKSPACE workspace) {
    const auto IT = std::ranges::find_if(m_participants, [&workspace](const auto& participant) { return participant.workspace == workspace; });
    if (IT != m_participants.end()) {
        workspace->m_forceRendering = true;
        return *IT;
    }

    auto& participant           = m_participants.emplace_back(SParticipant{
        .workspace     = workspace,
        .offset        = workspace->m_renderOffset->goal(),
        .alpha         = workspace->m_alpha->goal(),
        .forced        = workspace->m_forceRendering,
        .previewOffset = std::nullopt,
        .previewAlpha  = std::nullopt,
    });
    workspace->m_forceRendering = true;
    return participant;
}

void CSubSwipe::setPreviewOffset(PHLWORKSPACE workspace, const Vector2D& offset) {
    acquire(workspace).previewOffset = offset;
    workspace->m_renderOffset->setValueAndWarp(offset);
}

void CSubSwipe::setPreviewAlpha(PHLWORKSPACE workspace, float alpha) {
    acquire(workspace).previewAlpha = alpha;
    workspace->m_alpha->setValueAndWarp(alpha);
}

void CSubSwipe::restore(const SParticipant& participant) {
    const auto WORKSPACE = participant.workspace.lock();
    const auto MONITOR   = WORKSPACE ? WORKSPACE->m_monitor.lock() : nullptr;
    if (!WORKSPACE || !MONITOR)
        return;
    if (WORKSPACE != m_workspaceBegin && (WORKSPACE->visible() || MONITOR->m_activeWorkspace == WORKSPACE))
        return; // an activated neighbor must not regain its hidden preview baseline

    // only restore what still belongs to this gesture
    const bool OFFSET = participant.previewOffset && !WORKSPACE->m_renderOffset->isBeingAnimated() && WORKSPACE->m_renderOffset->value() == *participant.previewOffset &&
        WORKSPACE->m_renderOffset->goal() == *participant.previewOffset;
    const bool ALPHA = participant.previewAlpha && !WORKSPACE->m_alpha->isBeingAnimated() && WORKSPACE->m_alpha->value() == *participant.previewAlpha &&
        WORKSPACE->m_alpha->goal() == *participant.previewAlpha;
    if (!OFFSET && !ALPHA)
        return;

    if (const auto ORIGINAL = m_monitor.lock(); ORIGINAL && ORIGINAL != MONITOR)
        g_pHyprRenderer->damageMonitor(ORIGINAL);
    g_pHyprRenderer->damageMonitor(MONITOR);
    if (OFFSET)
        WORKSPACE->m_renderOffset->setValueAndWarp(participant.offset);
    if (ALPHA)
        WORKSPACE->m_alpha->setValueAndWarp(participant.alpha);
    WORKSPACE->updateWindowDecos();
}

void CSubSwipe::release(PHLWORKSPACE workspace) {
    if (!workspace)
        return;

    const auto IT = std::ranges::find_if(m_participants, [&workspace](const auto& participant) { return participant.workspace == workspace; });
    if (IT == m_participants.end())
        return;

    restore(*IT);
    workspace->m_forceRendering = IT->forced;
    m_participants.erase(IT);
}

void CSubSwipe::finishParticipants(PHLWORKSPACE animatedNeighbor) {
    for (const auto& participant : m_participants) {
        const auto WORKSPACE = participant.workspace.lock();
        if (!WORKSPACE)
            continue;
        if (WORKSPACE != m_workspaceBegin && WORKSPACE != animatedNeighbor)
            restore(participant);
        WORKSPACE->m_forceRendering = participant.forced;
    }
    m_participants.clear();
}

void CSubSwipe::releaseOtherParticipants(PHLWORKSPACE workspace) {
    std::erase_if(m_participants, [this, &workspace](const auto& participant) {
        const auto WORKSPACE = participant.workspace.lock();
        if (!WORKSPACE)
            return true;
        if (WORKSPACE == m_workspaceBegin || WORKSPACE == workspace)
            return false;

        restore(participant);
        WORKSPACE->m_forceRendering = participant.forced;
        return true;
    });
}

void CSubSwipe::restoreLayers() {
    const auto MONITOR = m_monitor.lock();
    if (!MONITOR || !MONITOR->m_activeWorkspace)
        return;

    const auto WORKSPACE = MONITOR->m_activeWorkspace;
    const auto FSWINDOW  = Fullscreen::controller()->getFullscreenWindow(WORKSPACE);
    const auto MODE      = FSWINDOW ? Fullscreen::controller()->getFullscreenModes(FSWINDOW).internal : Fullscreen::FSMODE_NONE;
    const auto SPACE     = WORKSPACE->space();
    const bool HIDE      = MODE == Fullscreen::FSMODE_FULLSCREEN &&
        (!FSWINDOW || !Fullscreen::controller()->layoutManagedFS(FSWINDOW) || (SPACE && SPACE->algorithm() && Fullscreen::controller()->hasFullscreen(WORKSPACE, true)));

    for (const auto& ls : MONITOR->m_layerSurfaceLayers[2]) {
        *ls->alpha()[Desktop::View::LS_ALPHA_FADE] = HIDE ? 0.F : 1.F;
    }
}

void CSubSwipe::reset() {
    resetListeners();
    m_workspaceBegin.reset();
    m_monitor.reset();
    m_delta            = 0;
    m_initialDirection = 0;
    m_avgSpeed         = 0;
    m_speedPoints      = 0;
}

void CSubSwipe::cancel() {
    resetListeners();
    for (const auto& participant : m_participants) {
        restore(participant);
        if (const auto WORKSPACE = participant.workspace.lock())
            WORKSPACE->m_forceRendering = participant.forced;
    }
    m_participants.clear();
    restoreLayers();
    stopCarry();
    reset();
}

int64_t CSubSwipe::swipeDistance(int64_t native) const {
    const auto VERTICAL = Cfg::verticalSwipeDistance();
    const auto DIST     = m_axis == AXIS_SUB && VERTICAL > 0 ? VERTICAL : native;
    return std::clamp(DIST, sc<int64_t>(1LL), sc<int64_t>(UINT32_MAX));
}

void CSubSwipe::computeTargets() {
    m_idLeft      = WORKSPACE_INVALID;
    m_idRight     = WORKSPACE_INVALID;
    m_createLeft  = false;
    m_createRight = false;

    const auto POS = posOf(m_workspaceBegin);
    if (!POS)
        return;

    const auto MON  = m_monitor.lock();
    const auto MAP  = layout(MON);
    const auto EDGE = m_axis == AXIS_SUB ? Cfg::subEdge() : Cfg::groupEdge();

    // never create from an empty workspace: that would only chain empty ones
    const bool CANCREATE = EDGE == Cfg::EDGE_CREATE && m_workspaceBegin->getWindowCount() > 0;

    WORKSPACEID prev = WORKSPACE_INVALID, next = WORKSPACE_INVALID;
    bool        createPrev = false, createNext = false;

    if (m_axis == AXIS_SUB) {
        const auto IT = MAP.find(POS->group);
        if (IT == MAP.end())
            return;

        const auto& SUBS = IT->second;
        const auto  HERE = std::ranges::find(SUBS, POS->sub);
        if (HERE == SUBS.end())
            return;

        if (HERE != SUBS.begin())
            prev = encode(POS->group, *std::prev(HERE));
        else if (EDGE == Cfg::EDGE_WRAP && SUBS.size() > 1)
            prev = encode(POS->group, SUBS.back());
        else if (CANCREATE && POS->sub > 1) {
            prev       = encode(POS->group, POS->sub - 1);
            createPrev = true;
        }

        if (std::next(HERE) != SUBS.end())
            next = encode(POS->group, *std::next(HERE));
        else if (EDGE == Cfg::EDGE_WRAP && SUBS.size() > 1)
            next = encode(POS->group, SUBS.front());
        else if (CANCREATE) {
            next       = encode(POS->group, SUBS.back() + 1);
            createNext = true;
        }
    } else if (m_carry || m_allGroups) {
        // carrying a window or a 4-finger swipe: step to the adjacent group number, empty or not yet existing
        const auto TARGET = [&](int g, bool& create) {
            // 4 fingers: straight across the grid, same sub number (2.2 -> 1.2)
            if (m_allGroups) {
                create = !MAP.contains(g) || !std::ranges::contains(MAP.at(g), POS->sub);
                return encode(g, POS->sub);
            }

            if (MAP.contains(g))
                return encode(g, g_state.entrySub(g, m_rowMode, MAP));

            create = true;
            return encode(g, m_rowMode ? g_state.m_row : 1);
        };

        if (POS->group > 1)
            prev = TARGET(POS->group - 1, createPrev);
        if (POS->group < MAX_GROUP)
            next = TARGET(POS->group + 1, createNext);
    } else {
        std::vector<int> groups;
        for (const auto& [g, _] : MAP) {
            if (g != POS->group && groupHasWindows(g, MON))
                groups.push_back(g);
        }

        const auto         ABOVE = std::ranges::upper_bound(groups, POS->group);

        std::optional<int> left, right;
        if (ABOVE != groups.begin())
            left = *std::prev(ABOVE);
        else if (EDGE == Cfg::EDGE_WRAP && !groups.empty())
            left = groups.back();

        if (ABOVE != groups.end())
            right = *ABOVE;
        else if (EDGE == Cfg::EDGE_WRAP && !groups.empty())
            right = groups.front();

        if (left)
            prev = encode(*left, g_state.entrySub(*left, m_rowMode, MAP));
        else if (CANCREATE) {
            // highest unused group before this one
            for (int g = POS->group - 1; g >= 1; --g) {
                if (!MAP.contains(g)) {
                    prev       = encode(g, m_rowMode ? g_state.m_row : 1);
                    createPrev = true;
                    break;
                }
            }
        }
        if (right)
            next = encode(*right, g_state.entrySub(*right, m_rowMode, MAP));
        else if (CANCREATE) {
            // lowest unused group after this one, entered on sub 1 (or the current row in row mode)
            for (int g = POS->group + 1; g <= MAX_GROUP; ++g) {
                if (!MAP.contains(g)) {
                    next       = encode(g, m_rowMode ? g_state.m_row : 1);
                    createNext = true;
                    break;
                }
            }
        }
    }

    // "left" is drawn left / above, "right" right / below
    if (m_axis == AXIS_SUB && Cfg::subsAbove()) {
        m_idLeft      = next;
        m_idRight     = prev;
        m_createLeft  = createNext;
        m_createRight = createPrev;
    } else {
        m_idLeft      = prev;
        m_idRight     = next;
        m_createLeft  = createPrev;
        m_createRight = createNext;
    }
}

// target doesn't exist yet (it's created on release): only the current workspace moves
void CSubSwipe::slideBeginOnly(double swipeDistance, double xDistance, double yDistance) {
    g_pHyprRenderer->damageMonitor(m_monitor.lock());

    if (m_axis == AXIS_SUB)
        setPreviewOffset(m_workspaceBegin, Vector2D(0.0, ((-m_delta) / swipeDistance) * yDistance));
    else
        setPreviewOffset(m_workspaceBegin, Vector2D(((-m_delta) / swipeDistance) * xDistance, 0.0));

    m_workspaceBegin->updateWindowDecos();
}

void CSubSwipe::compensateCarry() {
    const auto WINDOW = m_carry.lock();
    const auto WS     = m_carryWs.lock();

    if (!WINDOW || !WS || WINDOW->m_workspace != WS) {
        stopCarry();
        return;
    }

    WINDOW->presentation().setFloatingOffset(-WS->m_renderOffset->value());

    if (!isGestureInProgress() && !WS->m_renderOffset->isBeingAnimated())
        stopCarry();
}

void CSubSwipe::stopCarry() {
    if (const auto WINDOW = m_carry.lock())
        WINDOW->presentation().clearFloatingOffset();

    m_carry.reset();
    m_carryWs.reset();
}

bool CSubSwipe::begin(eAxis axis, bool rowMode, bool carry, bool allGroups) {
    if (isGestureInProgress())
        return false;

    const auto MONITOR = Desktop::focusState()->monitor();
    if (!MONITOR || !MONITOR->m_enabled || !MONITOR->m_activeWorkspace)
        return false;

    const auto PWORKSPACE = MONITOR->m_activeWorkspace;

    if (!posOf(PWORKSPACE))
        return false;

    m_workspaceBegin   = PWORKSPACE;
    m_monitor          = MONITOR;
    m_axis             = axis;
    m_rowMode          = rowMode;
    m_carryMode        = carry;
    m_allGroups        = allGroups;
    m_delta            = 0;
    m_avgSpeed         = 0;
    m_speedPoints      = 0;
    m_initialDirection = 0;

    stopCarry(); // a previous carry still settling
    if (carry) {
        const auto WINDOW = Desktop::focusState()->window();
        if (WINDOW && WINDOW->m_workspace == PWORKSPACE && !(WINDOW->m_state & Desktop::View::WINDOW_STATE_PINNED)) {
            m_carry   = WINDOW;
            m_carryWs = PWORKSPACE;
        }
    }

    computeTargets();

    if (m_idLeft == WORKSPACE_INVALID && m_idRight == WORKSPACE_INVALID) {
        stopCarry();
        m_workspaceBegin.reset();
        m_monitor.reset();
        return false;
    }

    LOG(Log::DEBUG, "[hyprsubs] swipe begin from {} ({}, row mode {}): left {}, right {}", PWORKSPACE->displayName(), axis == AXIS_SUB ? "sub" : "group",
        rowMode, m_idLeft, m_idRight);

    const auto VALIDATE = [this] {
        if (!validOrigin())
            cancel();
    };
    m_listeners.monitorDisconnect       = MONITOR->m_events.disconnect.listen([this] { cancel(); });
    m_listeners.monitorDestroy          = MONITOR->m_events.destroy.listen([this] { cancel(); });
    m_listeners.workspaceMonitorChanged = PWORKSPACE->m_events.monitorChanged.listen(VALIDATE);
    m_listeners.workspaceActiveChanged  = PWORKSPACE->m_events.activeChanged.listen(VALIDATE);

    if (carry && !m_carryFrame) {
        m_carryFrame = Event::bus()->m_events.render.pre.listen([this](PHLMONITOR) {
            if (m_carry)
                compensateCarry();
        });
    }

    const auto FSWINDOW         = Fullscreen::controller()->getFullscreenWindow(PWORKSPACE);
    const auto INTERNAL_FS_MODE = FSWINDOW ? Fullscreen::controller()->getFullscreenModes(FSWINDOW).internal : Fullscreen::FSMODE_NONE;

    if (INTERNAL_FS_MODE == Fullscreen::FSMODE_FULLSCREEN) {
        for (auto const& ls : MONITOR->m_layerSurfaceLayers[2]) {
            *ls->alpha()[Desktop::View::LS_ALPHA_FADE] = 1.F;
        }
    }

    return true;
}

void CSubSwipe::update(double delta) {
    if (!isGestureInProgress())
        return;
    if (!validOrigin()) {
        cancel();
        return;
    }

    static auto  PSWIPEDIST             = CConfigValue<Config::INTEGER>("gestures:workspace_swipe_distance");
    static auto  PSWIPEDIRLOCK          = CConfigValue<Config::INTEGER>("gestures:workspace_swipe_direction_lock");
    static auto  PSWIPEDIRLOCKTHRESHOLD = CConfigValue<Config::INTEGER>("gestures:workspace_swipe_direction_lock_threshold");
    static auto  PSWIPEFOREVER          = CConfigValue<Config::INTEGER>("gestures:workspace_swipe_forever");
    static auto  PWORKSPACEGAP          = CConfigValue<Config::INTEGER>("general:gaps_workspaces");

    const auto   SWIPEDISTANCE = swipeDistance(*PSWIPEDIST);
    const auto   XDISTANCE     = m_monitor->m_size.x + *PWORKSPACEGAP;
    const auto   YDISTANCE     = m_monitor->m_size.y + *PWORKSPACEGAP;
    const bool   VERTANIMS     = m_axis == AXIS_SUB;
    const double d             = m_delta - delta;
    m_delta                    = delta;

    m_avgSpeed = (m_avgSpeed * m_speedPoints + abs(d)) / (m_speedPoints + 1);
    m_speedPoints++;

    acquire(m_workspaceBegin);

    m_delta = std::clamp(m_delta, sc<double>(-SWIPEDISTANCE), sc<double>(SWIPEDISTANCE));

    // nothing on that side: rubber-band back
    if ((m_delta < 0 && m_idLeft == WORKSPACE_INVALID) || (m_delta > 0 && m_idRight == WORKSPACE_INVALID)) {
        m_delta = 0;
        releaseOtherParticipants(nullptr);
        g_pHyprRenderer->damageMonitor(m_monitor.lock());
        setPreviewOffset(m_workspaceBegin, Vector2D(0.0, 0.0));
        return;
    }

    if (*PSWIPEDIRLOCK) {
        if (m_initialDirection != 0 && m_initialDirection != (m_delta < 0 ? -1 : 1))
            m_delta = 0;
        else if (m_initialDirection == 0 && abs(m_delta) > *PSWIPEDIRLOCKTHRESHOLD)
            m_initialDirection = m_delta < 0 ? -1 : 1;
    }

    const bool TOLEFT     = m_delta < 0;
    const auto PWORKSPACE = workspaceByID(TOLEFT ? m_idLeft : m_idRight);

    releaseOtherParticipants(PWORKSPACE);

    if (!PWORKSPACE) {
        if (TOLEFT ? m_createLeft : m_createRight)
            slideBeginOnly(SWIPEDISTANCE, XDISTANCE, YDISTANCE);
        else
            m_delta = 0;
        return;
    }

    setPreviewAlpha(PWORKSPACE, 1.f);

    // the target sits one screen to the left / above (or right / below) of the current one
    const double SIDE = TOLEFT ? -1.0 : 1.0;
    if (VERTANIMS) {
        setPreviewOffset(PWORKSPACE, Vector2D(0.0, ((-m_delta) / SWIPEDISTANCE) * YDISTANCE + SIDE * YDISTANCE));
        setPreviewOffset(m_workspaceBegin, Vector2D(0.0, ((-m_delta) / SWIPEDISTANCE) * YDISTANCE));
    } else {
        setPreviewOffset(PWORKSPACE, Vector2D(((-m_delta) / SWIPEDISTANCE) * XDISTANCE + SIDE * XDISTANCE, 0.0));
        setPreviewOffset(m_workspaceBegin, Vector2D(((-m_delta) / SWIPEDISTANCE) * XDISTANCE, 0.0));
    }

    PWORKSPACE->updateWindowDecos();

    g_pHyprRenderer->damageMonitor(m_monitor.lock());

    m_workspaceBegin->updateWindowDecos();

    if (*PSWIPEFOREVER) {
        if (abs(m_delta) >= SWIPEDISTANCE) {
            const auto AXIS    = m_axis;
            const auto ROWMODE = m_rowMode;
            const auto CARRY   = m_carryMode;
            const auto ALL     = m_allGroups;
            if (endSegment() && !isGestureInProgress())
                begin(AXIS, ROWMODE, CARRY, ALL);
        }
    }
}

void CSubSwipe::end() {
    endSegment();
}

bool CSubSwipe::endSegment() {
    if (!isGestureInProgress())
        return false;
    if (!validOrigin()) {
        cancel();
        return false;
    }

    const auto  ORIGIN       = m_workspaceBegin;
    const auto  MONITOR      = m_monitor.lock();
    const auto  OWNS_SESSION = [this, &ORIGIN, &MONITOR] { return m_workspaceBegin == ORIGIN && m_monitor == MONITOR; };

    static auto PSWIPEPERC    = CConfigValue<Config::FLOAT>("gestures:workspace_swipe_cancel_ratio");
    static auto PSWIPEDIST    = CConfigValue<Config::INTEGER>("gestures:workspace_swipe_distance");
    static auto PSWIPEFORC    = CConfigValue<Config::INTEGER>("gestures:workspace_swipe_min_speed_to_force");
    static auto PWORKSPACEGAP = CConfigValue<Config::INTEGER>("general:gaps_workspaces");
    const bool  VERTANIMS     = m_axis == AXIS_SUB;

    // our own activation emits the same signals that invalidate an externally replaced swipe
    resetListeners();

    const auto SWIPEDISTANCE = swipeDistance(*PSWIPEDIST);

    auto       PWORKSPACER = workspaceByID(m_idRight);
    auto       PWORKSPACEL = workspaceByID(m_idLeft);

    const auto RENDEROFFSETMIDDLE = m_workspaceBegin->m_renderOffset->value();
    const auto XDISTANCE          = MONITOR->m_size.x + *PWORKSPACEGAP;
    const auto YDISTANCE          = MONITOR->m_size.y + *PWORKSPACEGAP;

    const bool TOLEFT = m_delta < 0;
    const bool REVERT = (abs(m_delta) < SWIPEDISTANCE * *PSWIPEPERC && (*PSWIPEFORC == 0 || (*PSWIPEFORC != 0 && m_avgSpeed < *PSWIPEFORC))) || abs(m_delta) < 2 ||
        (TOLEFT && !PWORKSPACEL && !m_createLeft) || (!TOLEFT && !PWORKSPACER && !m_createRight);

    PHLWORKSPACE pTarget = nullptr;

    if (REVERT) {
        if (abs(m_delta) < 2) {
            if (PWORKSPACEL != m_workspaceBegin)
                release(PWORKSPACEL);
            if (PWORKSPACER != m_workspaceBegin)
                release(PWORKSPACER);
            m_workspaceBegin->m_renderOffset->setValueAndWarp(Vector2D(0, 0));
        } else {
            if (TOLEFT) {
                if (PWORKSPACEL) {
                    if (VERTANIMS)
                        *PWORKSPACEL->m_renderOffset = Vector2D{0.0, -YDISTANCE};
                    else
                        *PWORKSPACEL->m_renderOffset = Vector2D{-XDISTANCE, 0.0};
                }
            } else if (PWORKSPACER) {
                if (VERTANIMS)
                    *PWORKSPACER->m_renderOffset = Vector2D{0.0, YDISTANCE};
                else
                    *PWORKSPACER->m_renderOffset = Vector2D{XDISTANCE, 0.0};
            }

            *m_workspaceBegin->m_renderOffset = Vector2D();
        }
    } else {
        pTarget                 = TOLEFT ? PWORKSPACEL : PWORKSPACER;
        const auto RENDEROFFSET = pTarget ? pTarget->m_renderOffset->value() : Vector2D();

        if (!pTarget) {
            pTarget = State::workspaceState()->createNumbered(Workspace::SWorkspaceNumberedID{TOLEFT ? m_idLeft : m_idRight}, MONITOR);
            if (!OWNS_SESSION())
                return false;
            if (!pTarget || !validOrigin()) {
                cancel();
                return false;
            }
        }

        // the carried window moves first, so it's already on the target when it becomes active
        if (const auto WINDOW = m_carry.lock()) {
            (void)Config::Actions::moveToWorkspace(pTarget, true, WINDOW);
            m_carryWs = pTarget;
        }

        // a row-mode group swipe keeps the remembered row
        g_state.m_keepRow = m_axis == AXIS_GROUP && m_rowMode;
        MONITOR->changeWorkspace(pTarget);
        g_state.m_keepRow = false;
        pTarget->ready();
        if (!OWNS_SESSION())
            return false;

        if (!MONITOR->m_enabled || ORIGIN->m_monitor != MONITOR || MONITOR->m_activeWorkspace != pTarget) {
            cancel();
            return false;
        }

        pTarget->m_renderOffset->setValue(RENDEROFFSET);
        pTarget->m_alpha->setValueAndWarp(1.f);

        m_workspaceBegin->m_renderOffset->setValue(RENDEROFFSETMIDDLE);
        if (VERTANIMS)
            *m_workspaceBegin->m_renderOffset = Vector2D(0.0, TOLEFT ? YDISTANCE : -YDISTANCE);
        else
            *m_workspaceBegin->m_renderOffset = Vector2D(TOLEFT ? XDISTANCE : -XDISTANCE, 0.0);
        m_workspaceBegin->m_alpha->setValueAndWarp(1.f);

        g_pInputManager->unconstrainMouse();

        LOG(Log::DEBUG, "[hyprsubs] ended swipe to {}", pTarget->displayName());
    }

    g_pHyprRenderer->damageMonitor(MONITOR);

    finishParticipants(pTarget);
    restoreLayers();
    reset();
    g_pInputManager->refocus();

    const auto LANDED = pTarget ? pTarget : ORIGIN;
    if (const auto WINDOW = m_carry.lock(); WINDOW && WINDOW->m_workspace == LANDED)
        Desktop::focusState()->fullWindowFocus(WINDOW, Desktop::FOCUS_REASON_KEYBIND);

    return true;
}
