#pragma once

#include "Subs.hpp"

#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>

// Copy of Hyprland v0.56.2 CUnifiedWorkspaceSwipeGesture
// (src/managers/input/UnifiedWorkspaceSwipeGesture.cpp) with three changes:
//  1. targets come from the plugin instead of "m-1" / "m+1"
//  2. no numeric ID order checks
//  3. horizontal vs vertical comes from the gesture, not the animation style
// Creating past the last group / sub is the plugin's swipe_group_edge / swipe_sub_edge = create,
// not workspace_swipe_create_new.
class CSubSwipe {
  public:
    // false if there is nothing to swipe to.
    // carry: the focused window comes along (stays under the fingers, lands on the target).
    bool begin(Subs::eAxis axis, bool rowMode, bool carry = false);
    void update(double delta);
    void end();

    bool isGestureInProgress();

    double m_delta = 0;

  private:
    void         computeTargets();
    int64_t      swipeDistance(int64_t native) const;
    void         slideBeginOnly(double swipeDistance, double xDistance, double yDistance);
    void         compensateCarry();
    void         stopCarry();

    PHLWORKSPACE  m_workspaceBegin = nullptr;
    PHLMONITORREF m_monitor;

    int           m_initialDirection = 0;
    float         m_avgSpeed         = 0;
    int           m_speedPoints      = 0;

    Subs::eAxis   m_axis    = Subs::AXIS_GROUP;
    bool          m_rowMode = false;

    // "left" = previous group / sub, "right" = next
    WORKSPACEID   m_idLeft  = WORKSPACE_INVALID;
    WORKSPACEID   m_idRight = WORKSPACE_INVALID;

    // that side's target doesn't exist yet and is created when the swipe commits
    bool          m_createLeft  = false;
    bool          m_createRight = false;

    // carried window: every frame its m_floatingOffset cancels m_carryWs's slide offset, so it
    // stays put on screen. Runs past the gesture until that workspace's animation settles.
    bool                m_carryMode = false;
    PHLWINDOWREF        m_carry;
    PHLWORKSPACEREF     m_carryWs;
    CHyprSignalListener m_carryFrame;
};

inline UP<CSubSwipe> g_pSubSwipe;
