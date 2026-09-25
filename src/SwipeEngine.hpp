#pragma once

#include "Subs.hpp"

#include <hyprland/src/helpers/memory/Memory.hpp>

// Copy of Hyprland v0.56.2 CUnifiedWorkspaceSwipeGesture
// (src/managers/input/UnifiedWorkspaceSwipeGesture.cpp) with three changes:
//  1. targets come from the plugin instead of "m-1" / "m+1"
//  2. no numeric ID order checks
//  3. horizontal vs vertical comes from the gesture, not the animation style
// workspace_swipe_create_new is not supported: swipes never create subs.
class CSubSwipe {
  public:
    // false if there is nothing to swipe to
    bool begin(Subs::eAxis axis, bool rowMode);
    void update(double delta);
    void end();

    bool isGestureInProgress();

    double m_delta = 0;

  private:
    void         computeTargets();
    int64_t      swipeDistance(int64_t native) const;

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
};

inline UP<CSubSwipe> g_pSubSwipe;
