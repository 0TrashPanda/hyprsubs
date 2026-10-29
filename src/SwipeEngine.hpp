#pragma once

#include "Subs.hpp"

#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>

#include <hyprutils/math/Vector2D.hpp>

#include <optional>
#include <vector>

// Copy of Hyprland v0.57.0 CUnifiedWorkspaceSwipeGesture
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
    // allGroups: step to the adjacent group number on the same sub, instead of skipping groups without windows.
    bool begin(Subs::eAxis axis, bool rowMode, bool carry = false, bool allGroups = false);
    void update(double delta);
    void end();
    void cancel();

    bool isGestureInProgress();

    double m_delta = 0;

  private:
    struct SParticipant {
        PHLWORKSPACEREF                          workspace;
        Hyprutils::Math::Vector2D                offset;
        float                                    alpha  = 1.F;
        bool                                     forced = false;
        std::optional<Hyprutils::Math::Vector2D> previewOffset;
        std::optional<float>                     previewAlpha;
    };

    SParticipant& acquire(PHLWORKSPACE workspace);
    void          setPreviewOffset(PHLWORKSPACE workspace, const Hyprutils::Math::Vector2D& offset);
    void          setPreviewAlpha(PHLWORKSPACE workspace, float alpha);
    void          restore(const SParticipant& participant);
    void          release(PHLWORKSPACE workspace);
    void          releaseOtherParticipants(PHLWORKSPACE workspace);
    void          finishParticipants(PHLWORKSPACE animatedNeighbor = nullptr);
    void          restoreLayers();
    void          reset();
    void          resetListeners();
    bool          validOrigin() const;
    bool          endSegment();

    void          computeTargets();
    int64_t       swipeDistance(int64_t native) const;
    void          slideBeginOnly(double swipeDistance, double xDistance, double yDistance);
    void          compensateCarry();
    void          stopCarry();

    std::vector<SParticipant> m_participants;
    PHLWORKSPACE              m_workspaceBegin = nullptr;
    PHLMONITORREF             m_monitor;

    int                       m_initialDirection = 0;
    float                     m_avgSpeed         = 0;
    int                       m_speedPoints      = 0;

    Subs::eAxis               m_axis      = Subs::AXIS_GROUP;
    bool                      m_rowMode   = false;
    bool                      m_allGroups = false;

    // "left" = previous group / sub, "right" = next
    Subs::WORKSPACEID         m_idLeft  = Subs::WORKSPACE_INVALID;
    Subs::WORKSPACEID         m_idRight = Subs::WORKSPACE_INVALID;

    // that side's target doesn't exist yet and is created when the swipe commits
    bool                      m_createLeft  = false;
    bool                      m_createRight = false;

    struct {
        CHyprSignalListener monitorDisconnect;
        CHyprSignalListener monitorDestroy;
        CHyprSignalListener workspaceMonitorChanged;
        CHyprSignalListener workspaceActiveChanged;
    } m_listeners;

    // carried window: every frame its floating offset cancels m_carryWs's slide offset, so it
    // stays put on screen. Runs past the gesture until that workspace's animation settles.
    bool                m_carryMode = false;
    PHLWINDOWREF        m_carry;
    PHLWORKSPACEREF     m_carryWs;
    CHyprSignalListener m_carryFrame;
};

inline UP<CSubSwipe> g_pSubSwipe;
