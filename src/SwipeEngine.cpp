#include "SwipeEngine.hpp"
#include "Config.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>

#include <algorithm>

using namespace Subs;

bool CSubSwipe::isGestureInProgress() {
    return !!m_workspaceBegin;
}

int64_t CSubSwipe::swipeDistance(int64_t native) const {
    const auto VERTICAL = Cfg::verticalSwipeDistance();
    const auto DIST     = m_axis == AXIS_SUB && VERTICAL > 0 ? VERTICAL : native;
    return std::clamp(DIST, sc<int64_t>(1LL), sc<int64_t>(UINT32_MAX));
}

void CSubSwipe::computeTargets() {
    m_idLeft  = WORKSPACE_INVALID;
    m_idRight = WORKSPACE_INVALID;

    const auto POS = posOf(m_workspaceBegin);
    if (!POS)
        return;

    const auto MON  = m_monitor.lock();
    const auto MAP  = layout(MON);
    const bool WRAP = Cfg::swipeWrap();

    if (m_axis == AXIS_SUB) {
        const auto IT = MAP.find(POS->group);
        if (IT == MAP.end())
            return;

        const auto& SUBS = IT->second;
        const auto  HERE = std::ranges::find(SUBS, POS->sub);
        if (HERE == SUBS.end() || SUBS.size() < 2)
            return;

        if (HERE != SUBS.begin())
            m_idLeft = encode(POS->group, *std::prev(HERE));
        else if (WRAP)
            m_idLeft = encode(POS->group, SUBS.back());

        if (std::next(HERE) != SUBS.end())
            m_idRight = encode(POS->group, *std::next(HERE));
        else if (WRAP)
            m_idRight = encode(POS->group, SUBS.front());

        // "left" is drawn above, "right" below
        if (Cfg::subsAbove())
            std::swap(m_idLeft, m_idRight);

        return;
    }

    std::vector<int> groups;
    for (const auto& [g, _] : MAP) {
        if (g != POS->group && groupHasWindows(g, MON))
            groups.push_back(g);
    }

    if (groups.empty())
        return;

    const auto ABOVE = std::ranges::upper_bound(groups, POS->group);

    std::optional<int> left, right;
    if (ABOVE != groups.begin())
        left = *std::prev(ABOVE);
    else if (WRAP)
        left = groups.back();

    if (ABOVE != groups.end())
        right = *ABOVE;
    else if (WRAP)
        right = groups.front();

    if (left)
        m_idLeft = encode(*left, g_state.entrySub(*left, m_rowMode, MAP));
    if (right)
        m_idRight = encode(*right, g_state.entrySub(*right, m_rowMode, MAP));
}

bool CSubSwipe::begin(eAxis axis, bool rowMode) {
    if (isGestureInProgress())
        return false;

    const auto PMONITOR = Desktop::focusState()->monitor();
    if (!PMONITOR)
        return false;

    const auto PWORKSPACE = PMONITOR->m_activeWorkspace;

    if (!posOf(PWORKSPACE))
        return false;

    m_workspaceBegin = PWORKSPACE;
    m_monitor        = Desktop::focusState()->monitor();
    m_axis           = axis;
    m_rowMode        = rowMode;

    computeTargets();

    if (m_idLeft == WORKSPACE_INVALID && m_idRight == WORKSPACE_INVALID) {
        m_workspaceBegin = nullptr;
        return false;
    }

    Log::logger->log(Log::DEBUG, "[hyprsubs] swipe begin from {} ({}, row mode {}): left {}, right {}", PWORKSPACE->m_id, axis == AXIS_SUB ? "sub" : "group", rowMode,
                     m_idLeft, m_idRight);

    m_delta       = 0;
    m_avgSpeed    = 0;
    m_speedPoints = 0;

    const auto FSWINDOW         = Fullscreen::controller()->getFullscreenWindow(PWORKSPACE);
    const auto INTERNAL_FS_MODE = FSWINDOW ? Fullscreen::controller()->getFullscreenModes(FSWINDOW).internal : Fullscreen::FSMODE_NONE;

    if (INTERNAL_FS_MODE == Fullscreen::FSMODE_FULLSCREEN) {
        for (auto const& ls : Desktop::focusState()->monitor()->m_layerSurfaceLayers[2]) {
            *ls->alpha()[Desktop::View::LS_ALPHA_FADE] = 1.F;
        }
    }

    return true;
}

void CSubSwipe::update(double delta) {
    if (!isGestureInProgress())
        return;

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

    const auto workspaceIDLeft  = m_idLeft;
    const auto workspaceIDRight = m_idRight;

    m_workspaceBegin->m_forceRendering = true;

    m_delta = std::clamp(m_delta, sc<double>(-SWIPEDISTANCE), sc<double>(SWIPEDISTANCE));

    // nothing on that side: rubber-band back
    if ((m_delta < 0 && workspaceIDLeft == WORKSPACE_INVALID) || (m_delta > 0 && workspaceIDRight == WORKSPACE_INVALID)) {
        m_delta = 0;
        g_pHyprRenderer->damageMonitor(m_monitor.lock());
        m_workspaceBegin->m_renderOffset->setValueAndWarp(Vector2D(0.0, 0.0));
        return;
    }

    if (*PSWIPEDIRLOCK) {
        if (m_initialDirection != 0 && m_initialDirection != (m_delta < 0 ? -1 : 1))
            m_delta = 0;
        else if (m_initialDirection == 0 && abs(m_delta) > *PSWIPEDIRLOCKTHRESHOLD)
            m_initialDirection = m_delta < 0 ? -1 : 1;
    }

    if (m_delta < 0) {
        const auto PWORKSPACE = State::workspaceState()->query().id(workspaceIDLeft).run();

        if (!PWORKSPACE) {
            m_delta = 0;
            return;
        }

        PWORKSPACE->m_forceRendering = true;
        PWORKSPACE->m_alpha->setValueAndWarp(1.f);

        if (workspaceIDLeft != workspaceIDRight && workspaceIDRight != WORKSPACE_INVALID && workspaceIDRight != m_workspaceBegin->m_id) {
            const auto PWORKSPACER = State::workspaceState()->query().id(workspaceIDRight).run();

            if (PWORKSPACER) {
                PWORKSPACER->m_forceRendering = false;
                PWORKSPACER->m_alpha->setValueAndWarp(0.f);
            }
        }

        if (VERTANIMS) {
            PWORKSPACE->m_renderOffset->setValueAndWarp(Vector2D(0.0, ((-m_delta) / SWIPEDISTANCE) * YDISTANCE - YDISTANCE));
            m_workspaceBegin->m_renderOffset->setValueAndWarp(Vector2D(0.0, ((-m_delta) / SWIPEDISTANCE) * YDISTANCE));
        } else {
            PWORKSPACE->m_renderOffset->setValueAndWarp(Vector2D(((-m_delta) / SWIPEDISTANCE) * XDISTANCE - XDISTANCE, 0.0));
            m_workspaceBegin->m_renderOffset->setValueAndWarp(Vector2D(((-m_delta) / SWIPEDISTANCE) * XDISTANCE, 0.0));
        }

        PWORKSPACE->updateWindowDecos();
    } else {
        const auto PWORKSPACE = State::workspaceState()->query().id(workspaceIDRight).run();

        if (!PWORKSPACE) {
            m_delta = 0;
            return;
        }

        PWORKSPACE->m_forceRendering = true;
        PWORKSPACE->m_alpha->setValueAndWarp(1.f);

        if (workspaceIDLeft != workspaceIDRight && workspaceIDLeft != WORKSPACE_INVALID && workspaceIDLeft != m_workspaceBegin->m_id) {
            const auto PWORKSPACEL = State::workspaceState()->query().id(workspaceIDLeft).run();

            if (PWORKSPACEL) {
                PWORKSPACEL->m_forceRendering = false;
                PWORKSPACEL->m_alpha->setValueAndWarp(0.f);
            }
        }

        if (VERTANIMS) {
            PWORKSPACE->m_renderOffset->setValueAndWarp(Vector2D(0.0, ((-m_delta) / SWIPEDISTANCE) * YDISTANCE + YDISTANCE));
            m_workspaceBegin->m_renderOffset->setValueAndWarp(Vector2D(0.0, ((-m_delta) / SWIPEDISTANCE) * YDISTANCE));
        } else {
            PWORKSPACE->m_renderOffset->setValueAndWarp(Vector2D(((-m_delta) / SWIPEDISTANCE) * XDISTANCE + XDISTANCE, 0.0));
            m_workspaceBegin->m_renderOffset->setValueAndWarp(Vector2D(((-m_delta) / SWIPEDISTANCE) * XDISTANCE, 0.0));
        }

        PWORKSPACE->updateWindowDecos();
    }

    g_pHyprRenderer->damageMonitor(m_monitor.lock());

    m_workspaceBegin->updateWindowDecos();

    if (*PSWIPEFOREVER) {
        if (abs(m_delta) >= SWIPEDISTANCE) {
            const auto AXIS    = m_axis;
            const auto ROWMODE = m_rowMode;
            end();
            begin(AXIS, ROWMODE);
        }
    }
}

void CSubSwipe::end() {
    if (!isGestureInProgress())
        return;

    static auto PSWIPEPERC    = CConfigValue<Config::FLOAT>("gestures:workspace_swipe_cancel_ratio");
    static auto PSWIPEDIST    = CConfigValue<Config::INTEGER>("gestures:workspace_swipe_distance");
    static auto PSWIPEFORC    = CConfigValue<Config::INTEGER>("gestures:workspace_swipe_min_speed_to_force");
    static auto PWORKSPACEGAP = CConfigValue<Config::INTEGER>("general:gaps_workspaces");
    const bool  VERTANIMS     = m_axis == AXIS_SUB;

    // commit
    const auto workspaceIDLeft  = m_idLeft;
    const auto workspaceIDRight = m_idRight;
    const auto SWIPEDISTANCE    = swipeDistance(*PSWIPEDIST);

    auto       PWORKSPACER = workspaceIDRight == WORKSPACE_INVALID ? nullptr : State::workspaceState()->query().id(workspaceIDRight).run();
    auto       PWORKSPACEL = workspaceIDLeft == WORKSPACE_INVALID ? nullptr : State::workspaceState()->query().id(workspaceIDLeft).run();

    const auto RENDEROFFSETMIDDLE = m_workspaceBegin->m_renderOffset->value();
    const auto XDISTANCE          = m_monitor->m_size.x + *PWORKSPACEGAP;
    const auto YDISTANCE          = m_monitor->m_size.y + *PWORKSPACEGAP;

    PHLWORKSPACE pSwitchedTo = nullptr;

    const bool   REVERT = (abs(m_delta) < SWIPEDISTANCE * *PSWIPEPERC && (*PSWIPEFORC == 0 || (*PSWIPEFORC != 0 && m_avgSpeed < *PSWIPEFORC))) || abs(m_delta) < 2 ||
        (m_delta < 0 && !PWORKSPACEL) || (m_delta > 0 && !PWORKSPACER);

    if (REVERT) {
        // revert
        if (abs(m_delta) < 2) {
            if (PWORKSPACEL)
                PWORKSPACEL->m_renderOffset->setValueAndWarp(Vector2D(0, 0));
            if (PWORKSPACER)
                PWORKSPACER->m_renderOffset->setValueAndWarp(Vector2D(0, 0));
            m_workspaceBegin->m_renderOffset->setValueAndWarp(Vector2D(0, 0));
        } else {
            if (m_delta < 0) {
                // to left

                if (PWORKSPACEL) {
                    if (VERTANIMS)
                        *PWORKSPACEL->m_renderOffset = Vector2D{0.0, -YDISTANCE};
                    else
                        *PWORKSPACEL->m_renderOffset = Vector2D{-XDISTANCE, 0.0};
                }
            } else if (PWORKSPACER) {
                // to right
                if (VERTANIMS)
                    *PWORKSPACER->m_renderOffset = Vector2D{0.0, YDISTANCE};
                else
                    *PWORKSPACER->m_renderOffset = Vector2D{XDISTANCE, 0.0};
            }

            *m_workspaceBegin->m_renderOffset = Vector2D();
        }

        pSwitchedTo = m_workspaceBegin;
    } else {
        const bool TOLEFT       = m_delta < 0;
        const auto PTARGET      = TOLEFT ? PWORKSPACEL : PWORKSPACER;
        const auto RENDEROFFSET = PTARGET->m_renderOffset->value();

        // a row-mode group swipe keeps the remembered row
        g_state.m_keepRow = m_axis == AXIS_GROUP && m_rowMode;
        m_monitor->changeWorkspace(PTARGET);
        g_state.m_keepRow = false;

        PTARGET->m_renderOffset->setValue(RENDEROFFSET);
        PTARGET->m_alpha->setValueAndWarp(1.f);

        m_workspaceBegin->m_renderOffset->setValue(RENDEROFFSETMIDDLE);
        if (VERTANIMS)
            *m_workspaceBegin->m_renderOffset = Vector2D(0.0, TOLEFT ? YDISTANCE : -YDISTANCE);
        else
            *m_workspaceBegin->m_renderOffset = Vector2D(TOLEFT ? XDISTANCE : -XDISTANCE, 0.0);
        m_workspaceBegin->m_alpha->setValueAndWarp(1.f);

        g_pInputManager->unconstrainMouse();

        Log::logger->log(Log::DEBUG, "[hyprsubs] ended swipe to {}", PTARGET->m_id);

        pSwitchedTo = PTARGET;
    }

    g_pHyprRenderer->damageMonitor(m_monitor.lock());

    if (PWORKSPACEL)
        PWORKSPACEL->m_forceRendering = false;
    if (PWORKSPACER)
        PWORKSPACER->m_forceRendering = false;
    m_workspaceBegin->m_forceRendering = false;

    m_workspaceBegin   = nullptr;
    m_initialDirection = 0;

    g_pInputManager->refocus();

    // apply alpha
    if (pSwitchedTo) {
        const auto FSWINDOW         = Fullscreen::controller()->getFullscreenWindow(pSwitchedTo);
        const auto FS_MODE_INTERNAL = FSWINDOW ? Fullscreen::controller()->getFullscreenModes(FSWINDOW).internal : Fullscreen::FSMODE_NONE;
        const bool HIDE             = FS_MODE_INTERNAL == Fullscreen::FSMODE_FULLSCREEN &&
            (!FSWINDOW || !Fullscreen::controller()->layoutManagedFS(FSWINDOW) ||
             (pSwitchedTo->m_space && pSwitchedTo->m_space->algorithm() && Fullscreen::controller()->hasFullscreen(pSwitchedTo, true)));

        for (auto const& ls : Desktop::focusState()->monitor()->m_layerSurfaceLayers[2]) {
            *ls->alpha()[Desktop::View::LS_ALPHA_FADE] = HIDE ? 0.F : 1.F;
        }
    }
}
