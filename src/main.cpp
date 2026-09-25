#include "Config.hpp"
#include "Subs.hpp"
#include "SwipeEngine.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/animation/WorkspaceAnimationController.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/EventManager.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/input/trackpad/gestures/WorkspaceSwipeGesture.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>

#include <hyprutils/string/String.hpp>
#include <hyprutils/string/VarList.hpp>

#include <algorithm>
#include <format>

using namespace Subs;
using namespace Hyprutils::String;

inline HANDLE PHANDLE = nullptr;

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

// ---------------------------------------------------------------- hooks

static CFunctionHook* g_pSwipeBeginHook  = nullptr;
static CFunctionHook* g_pSwipeUpdateHook = nullptr;
static CFunctionHook* g_pSwipeEndHook    = nullptr;
static CFunctionHook* g_pStartAnimHook   = nullptr;

// set when a swipe was handed to the native engine (gesture direction we don't handle)
static bool g_nativeSwipe = false;

using origSwipeBegin  = void (*)(CWorkspaceSwipeGesture*, const ITrackpadGesture::STrackpadGestureBegin&);
using origSwipeUpdate = void (*)(CWorkspaceSwipeGesture*, const ITrackpadGesture::STrackpadGestureUpdate&);
using origSwipeEnd    = void (*)(CWorkspaceSwipeGesture*, const ITrackpadGesture::STrackpadGestureEnd&);
using origStartAnim   = void (*)(PHLWORKSPACE, Animation::Workspace::eAnimationType, bool, bool, std::optional<std::string>);

static void hkSwipeBegin(CWorkspaceSwipeGesture* thisptr, const ITrackpadGesture::STrackpadGestureBegin& e) {
    std::optional<eAxis> axis;
    switch (e.direction) {
        case TRACKPAD_GESTURE_DIR_LEFT:
        case TRACKPAD_GESTURE_DIR_RIGHT:
        case TRACKPAD_GESTURE_DIR_HORIZONTAL: axis = AXIS_GROUP; break;
        case TRACKPAD_GESTURE_DIR_UP:
        case TRACKPAD_GESTURE_DIR_DOWN:
        case TRACKPAD_GESTURE_DIR_VERTICAL: axis = AXIS_SUB; break;
        default: break;
    }

    g_nativeSwipe = !axis;
    if (g_nativeSwipe) {
        ((origSwipeBegin)g_pSwipeBeginHook->m_original)(thisptr, e);
        return;
    }

    thisptr->ITrackpadGesture::begin(e);

    if (g_pSessionLockManager->isSessionLocked() || g_pSubSwipe->isGestureInProgress())
        return;

    const uint32_t FINGERS = e.swipe ? e.swipe->fingers : 3;
    const bool     ROWMODE = *axis == AXIS_GROUP && (FINGERS >= 4 || (g_state.m_rowMode && Cfg::rowModeFor3Finger()));

    g_pSubSwipe->begin(*axis, ROWMODE);
}

static void hkSwipeUpdate(CWorkspaceSwipeGesture* thisptr, const ITrackpadGesture::STrackpadGestureUpdate& e) {
    if (g_nativeSwipe) {
        ((origSwipeUpdate)g_pSwipeUpdateHook->m_original)(thisptr, e);
        return;
    }

    if (!g_pSubSwipe->isGestureInProgress())
        return;

    const float  DELTA = thisptr->distance(e);

    static auto  PSWIPEINVR = CConfigValue<Config::INTEGER>("gestures:workspace_swipe_invert");

    const double D = g_pSubSwipe->m_delta + (*PSWIPEINVR ? -DELTA : DELTA);
    g_pSubSwipe->update(D);
}

static void hkSwipeEnd(CWorkspaceSwipeGesture* thisptr, const ITrackpadGesture::STrackpadGestureEnd& e) {
    if (g_nativeSwipe) {
        g_nativeSwipe = false;
        ((origSwipeEnd)g_pSwipeEndHook->m_original)(thisptr, e);
        return;
    }

    if (!g_pSubSwipe->isGestureInProgress())
        return;

    g_pSubSwipe->end();
}

// "slide" <-> "slidevert", "slidefade" <-> "slidefadevert", keeping a trailing percentage.
// Anything else (fade, popin, ...) is left alone.
static std::optional<std::string> styleForAxis(const std::string& style, eAxis axis) {
    const bool VERT = axis == AXIS_SUB;

    CVarList   args(style, 0, 's');
    const auto BASE = args[0];

    std::string percent;
    if (args.size() > 1 && args[args.size() - 1].ends_with('%'))
        percent = " " + args[args.size() - 1];

    if (BASE.empty() || BASE == "slide" || BASE == "slidevert")
        return std::string{VERT ? "slidevert" : "slide"} + percent;
    if (BASE == "slidefade" || BASE == "slidefadevert")
        return std::string{VERT ? "slidefadevert" : "slidefade"} + percent;

    return std::nullopt;
}

static void hkStartAnimation(PHLWORKSPACE ws, Animation::Workspace::eAnimationType type, bool left, bool instant, std::optional<std::string> style) {
    if (g_pendingAnim.active && ws && !ws->m_isSpecialWorkspace && !instant) {
        // the style the original would read after applying the workspaces{In,Out} config
        std::string current = style.value_or("");
        if (!style) {
            const auto CFG = Config::animationTree()->getAnimationPropertyConfig(type == Animation::Workspace::ANIMATION_TYPE_IN ? "workspacesIn" : "workspacesOut");
            ws->m_alpha->setConfig(CFG);
            current = ws->m_alpha->getStyle();
        }

        if (const auto NEWSTYLE = styleForAxis(current, g_pendingAnim.axis)) {
            // left = true slides the new workspace in from the right / bottom
            left  = g_pendingAnim.forward != (g_pendingAnim.axis == AXIS_SUB && Cfg::subsAbove());
            style = NEWSTYLE;
        }
    }

    ((origStartAnim)g_pStartAnimHook->m_original)(ws, type, left, instant, style);
}

// ---------------------------------------------------------------- switching

static void scheduleChanged();

static std::optional<SPos> currentPos() {
    const auto MON = Desktop::focusState()->monitor();
    return MON ? posOf(MON->m_activeWorkspace) : std::nullopt;
}

static SDispatchResult err(const std::string& msg) {
    return {.success = false, .error = msg};
}

// Switch to (or move the focused window to) a sub, creating the workspace if needed.
static SDispatchResult go(int group, int sub, eAxis axis, bool forward, bool moveWindow, bool keepRow = false) {
    const auto MON = Desktop::focusState()->monitor();
    if (!MON)
        return err("hyprsubs: no focused monitor");

    PHLWINDOW window;
    if (moveWindow) {
        window = Desktop::focusState()->window();
        if (!window)
            return {};
    }

    const auto ID = encode(group, sub);
    auto       ws = State::workspaceState()->query().id(ID).run();
    if (!ws)
        ws = State::workspaceState()->create(ID, MON->m_id);
    if (!ws)
        return err(std::format("hyprsubs: failed to create workspace {}", ID));

    g_pendingAnim     = {.active = true, .axis = axis, .forward = forward};
    g_state.m_keepRow = keepRow;

    const auto RES = moveWindow ? Config::Actions::moveToWorkspace(ws, false, window) : Config::Actions::changeWorkspace(ws);

    g_pendingAnim.active = false;
    g_state.m_keepRow    = false;

    if (!RES)
        return err(RES.error().message);

    return {};
}

static std::optional<int> parseInt(const std::string& s) {
    if (!isNumber(s))
        return std::nullopt;
    try {
        return std::stoi(s);
    } catch (...) { return std::nullopt; }
}

static std::optional<int> parseGroup(const std::string& args) {
    const auto N = parseInt(trim(args));
    if (!N || *N < 1 || *N > MAX_GROUP)
        return std::nullopt;
    return N;
}

// next / prev / S inside the current group
static SDispatchResult toSub(const std::string& args, bool moveWindow) {
    const auto CUR = currentPos();
    if (!CUR)
        return err("hyprsubs: not on a sub workspace");

    const auto  MAP  = layout();
    const auto& SUBS = MAP.at(CUR->group);
    const auto  HERE = std::ranges::find(SUBS, CUR->sub);
    const auto  ARG  = trim(args);

    int         target  = 0;
    bool        forward = true;

    if (ARG == "next" || ARG == "prev") {
        if (SUBS.size() < 2)
            return {};

        forward = ARG == "next";
        if (forward)
            target = std::next(HERE) == SUBS.end() ? SUBS.front() : *std::next(HERE);
        else
            target = HERE == SUBS.begin() ? SUBS.back() : *std::prev(HERE);
    } else {
        const auto S = parseInt(ARG);
        if (!S || *S < 1)
            return err("hyprsubs: expected next, prev or a sub number");
        if (!std::ranges::contains(SUBS, *S))
            return err(std::format("hyprsubs: sub {}.{} doesn't exist", CUR->group, *S));
        if (*S == CUR->sub)
            return {};

        target  = *S;
        forward = *S > CUR->sub;
    }

    return go(CUR->group, target, AXIS_SUB, forward, moveWindow);
}

// SUPER + N / SUPER + SHIFT + N
static SDispatchResult toGroup(const std::string& args, bool moveWindow) {
    const auto N = parseGroup(args);
    if (!N)
        return err("hyprsubs: expected a group number (1-99)");

    const auto CUR = currentPos();
    if (CUR && CUR->group == *N)
        return toSub("next", moveWindow);

    const auto MAP = layout();
    const auto SUB = g_state.entrySub(*N, g_state.m_rowMode, MAP);

    return go(*N, SUB, AXIS_GROUP, !CUR || *N > CUR->group, moveWindow, g_state.m_rowMode);
}

// SUPER + CTRL (+ SHIFT) + N
static SDispatchResult toNewSub(const std::string& args, bool moveWindow) {
    const auto N = parseGroup(args);
    if (!N)
        return err("hyprsubs: expected a group number (1-99)");

    const auto CUR = currentPos();
    const auto SUB = g_state.newSubFor(*N, layout());

    if (CUR && CUR->group == *N)
        return go(*N, SUB, AXIS_SUB, SUB > CUR->sub, moveWindow);

    return go(*N, SUB, AXIS_GROUP, !CUR || *N > CUR->group, moveWindow);
}

static SDispatchResult groupCycle(const std::string& args) {
    const auto ARG = trim(args);
    if (ARG != "next" && ARG != "prev")
        return err("hyprsubs: expected next or prev");

    const auto MON = Desktop::focusState()->monitor();
    const auto CUR = currentPos();
    const auto MAP = layout(MON);

    std::vector<int> groups;
    for (const auto& [g, _] : MAP) {
        if ((!CUR || g != CUR->group) && groupHasWindows(g, MON))
            groups.push_back(g);
    }

    if (groups.empty())
        return {};

    const int  HEREG   = CUR ? CUR->group : 0;
    const bool FORWARD = ARG == "next";
    int        target  = 0;

    if (FORWARD) {
        const auto IT = std::ranges::upper_bound(groups, HEREG);
        target        = IT == groups.end() ? groups.front() : *IT;
    } else {
        const auto IT = std::ranges::lower_bound(groups, HEREG);
        target        = IT == groups.begin() ? groups.back() : *std::prev(IT);
    }

    return go(target, g_state.lastUsedSub(target, MAP), AXIS_GROUP, FORWARD, false);
}

static SDispatchResult rowMode(const std::string& args) {
    const auto ARG = trim(args);
    if (ARG == "on")
        g_state.m_rowMode = true;
    else if (ARG == "off")
        g_state.m_rowMode = false;
    else if (ARG == "toggle" || ARG.empty())
        g_state.m_rowMode = !g_state.m_rowMode;
    else
        return err("hyprsubs: expected on, off or toggle");

    scheduleChanged();
    return {};
}

// ---------------------------------------------------------------- hyprctl

// pretty: indented multi-line (hyprctl -j); otherwise one line (IPC event)
static std::string stateJson(bool pretty) {
    const auto CUR = currentPos();
    const auto MAP = layout();

    const std::string NL  = pretty ? "\n" : "";
    const std::string IND = pretty ? "  " : "";

    std::unordered_map<int, int> windows;
    for (const auto& ws : State::workspaceState()->workspaces()) {
        if (const auto POS = posOf(ws.lock()))
            windows[POS->group] += ws->getWindowCount();
    }

    std::string out = "{" + NL;

    if (CUR)
        out += std::format(R"({}"current": {{ "group": {}, "sub": {}, "workspace": {} }},)", IND, CUR->group, CUR->sub, encode(CUR->group, CUR->sub)) + NL;
    else
        out += IND + "\"current\": null," + NL;

    out += std::format(R"({}"row_mode": {},{}{}"row": {},{}{}"groups": [)", IND, g_state.m_rowMode, NL, IND, g_state.m_row, NL, IND);

    bool first = true;
    for (const auto& [g, subs] : MAP) {
        std::string list;
        for (const int s : subs) {
            list += (list.empty() ? "" : ", ") + std::to_string(s);
        }

        out += std::format(R"({}{}{}{{ "group": {}, "subs": [{}], "total": {}, "last_used": {}, "windows": {} }})", first ? "" : ",", NL, IND + IND, g, list, subs.size(),
                           g_state.lastUsedSub(g, MAP), windows[g]);
        first = false;
    }

    out += (MAP.empty() ? "" : NL + IND) + "]" + NL + "}";
    return out;
}

static std::string stateQuery(eHyprCtlOutputFormat format, std::string) {
    if (format == FORMAT_JSON)
        return stateJson(true);

    const auto MON = Desktop::focusState()->monitor();
    const auto CUR = currentPos();
    const auto MAP = layout();

    std::string out;
    if (CUR)
        out += std::format("current: {}.{} (workspace {})\n", CUR->group, CUR->sub, encode(CUR->group, CUR->sub));
    else
        out += std::format("current: not a sub (workspace {})\n", MON && MON->m_activeWorkspace ? MON->m_activeWorkspace->m_name : "?");

    out += std::format("row mode: {}, row: {}\n", g_state.m_rowMode ? "on" : "off", g_state.m_row);

    for (const auto& [g, subs] : MAP) {
        std::string list;
        for (const int s : subs) {
            list += (list.empty() ? "" : " ") + std::to_string(s);
        }
        out += std::format("group {}: subs [{}], last used {}\n", g, list, g_state.lastUsedSub(g, MAP));
    }

    return out;
}

// ---------------------------------------------------------------- change event

// Posts "hyprsubs>>{state json}" on the Hyprland event socket when the state changed.
// Deferred to an idle callback so bursts collapse into one event and window counts are settled.
static UP<SEventLoopDoLaterLock> g_emitLater;
static std::string               g_lastEmitted;

static void scheduleChanged() {
    if (g_emitLater)
        return;

    g_emitLater = g_pEventLoopManager->doLaterLock([] {
        g_emitLater.reset(); // already ran: removing it is a no-op

        auto json = stateJson(false);
        if (json == g_lastEmitted)
            return;

        g_lastEmitted = std::move(json);
        g_pEventManager->postEvent(SHyprIPCEvent{"hyprsubs", g_lastEmitted});
    });
}

// ---------------------------------------------------------------- init

void Cfg::registerValues(HANDLE handle) {
    rowMode        = makeShared<Config::Values::CBoolValue>("plugin:hyprsubs:row_mode", "row mode toggle state at startup", false);
    rowMode3Finger = makeShared<Config::Values::CBoolValue>("plugin:hyprsubs:row_mode_3finger", "3-finger horizontal swipe uses row mode while the toggle is on", false);
    const auto EDGEVALIDATOR = [](const std::string& v) -> std::expected<void, std::string> {
        if (v == "stop" || v == "wrap" || v == "create")
            return {};
        return std::unexpected("expected stop, wrap or create");
    };
    groupEdgeValue = makeShared<Config::Values::CStringValue>("plugin:hyprsubs:swipe_group_edge", "swipe past the first / last group: stop, wrap or create", "create",
                                                              Config::Values::SStringValueOptions{.validator = EDGEVALIDATOR});
    subEdgeValue   = makeShared<Config::Values::CStringValue>("plugin:hyprsubs:swipe_sub_edge", "swipe past the first / last sub: stop, wrap or create", "stop",
                                                              Config::Values::SStringValueOptions{.validator = EDGEVALIDATOR});
    above          = makeShared<Config::Values::CBoolValue>("plugin:hyprsubs:subs_above", "higher subs sit above the current one instead of below", false);
    verticalDistance = makeShared<Config::Values::CIntValue>("plugin:hyprsubs:vertical_swipe_distance",
                                                             "swipe distance for vertical (sub) swipes, 0 = gestures:workspace_swipe_distance", 0,
                                                             Config::Values::SIntValueOptions{.min = 0, .max = 5000});

    HyprlandAPI::addConfigValueV2(handle, rowMode);
    HyprlandAPI::addConfigValueV2(handle, rowMode3Finger);
    HyprlandAPI::addConfigValueV2(handle, groupEdgeValue);
    HyprlandAPI::addConfigValueV2(handle, subEdgeValue);
    HyprlandAPI::addConfigValueV2(handle, above);
    HyprlandAPI::addConfigValueV2(handle, verticalDistance);
}

static void* findFunction(const std::string& name, const std::string& demangled) {
    for (const auto& fn : HyprlandAPI::findFunctionsByName(PHANDLE, name)) {
        if (fn.demangled.starts_with(demangled))
            return fn.address;
    }
    return nullptr;
}

static void fail(const std::string& msg) {
    HyprlandAPI::addNotification(PHANDLE, "[hyprsubs] " + msg, CHyprColor{1.0, 0.2, 0.2, 1.0}, 10000);
    throw std::runtime_error("[hyprsubs] " + msg);
}

static CHyprSignalListener              g_activeListener;
static CHyprSignalListener              g_reloadListener;
static std::vector<CHyprSignalListener> g_changeListeners;
static SP<SHyprCtlCommand>  g_hyprctlCommand;

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();
    if (HASH != CLIENT_HASH)
        fail("version mismatch: built against different Hyprland headers than the running Hyprland");

    Cfg::registerValues(handle);

    struct SHookSpec {
        CFunctionHook** hook;
        const char*     name;
        const char*     demangled;
        void*           destination;
    };

    const SHookSpec HOOKS[] = {
        {&g_pSwipeBeginHook, "begin", "CWorkspaceSwipeGesture::begin(", (void*)&hkSwipeBegin},
        {&g_pSwipeUpdateHook, "update", "CWorkspaceSwipeGesture::update(", (void*)&hkSwipeUpdate},
        {&g_pSwipeEndHook, "end", "CWorkspaceSwipeGesture::end(", (void*)&hkSwipeEnd},
        {&g_pStartAnimHook, "startAnimation", "Animation::Workspace::startAnimation(", (void*)&hkStartAnimation},
    };

    for (const auto& h : HOOKS) {
        const auto ADDR = findFunction(h.name, h.demangled);
        if (!ADDR)
            fail(std::format("couldn't find {}", h.demangled));

        *h.hook = HyprlandAPI::createFunctionHook(handle, ADDR, h.destination);
        if (!*h.hook || !(*h.hook)->hook())
            fail(std::format("couldn't hook {}", h.demangled));
    }

    g_pSubSwipe = makeUnique<CSubSwipe>();

    HyprlandAPI::addDispatcherV2(handle, "hyprsubs:group", [](std::string a) { return toGroup(a, false); });
    HyprlandAPI::addDispatcherV2(handle, "hyprsubs:movegroup", [](std::string a) { return toGroup(a, true); });
    HyprlandAPI::addDispatcherV2(handle, "hyprsubs:newsub", [](std::string a) { return toNewSub(a, false); });
    HyprlandAPI::addDispatcherV2(handle, "hyprsubs:movenewsub", [](std::string a) { return toNewSub(a, true); });
    HyprlandAPI::addDispatcherV2(handle, "hyprsubs:sub", [](std::string a) { return toSub(a, false); });
    HyprlandAPI::addDispatcherV2(handle, "hyprsubs:movesub", [](std::string a) { return toSub(a, true); });
    HyprlandAPI::addDispatcherV2(handle, "hyprsubs:groupcycle", [](std::string a) { return groupCycle(a); });
    HyprlandAPI::addDispatcherV2(handle, "hyprsubs:rowmode", [](std::string a) { return rowMode(a); });

    g_hyprctlCommand = HyprlandAPI::registerHyprCtlCommand(handle, SHyprCtlCommand{.name = "hyprsubs", .exact = true, .fn = stateQuery});

    g_activeListener = Event::bus()->m_events.workspace.active.listen([](PHLWORKSPACE ws) { g_state.onWorkspaceActive(ws); });

    auto& EV = Event::bus()->m_events;
    g_changeListeners.emplace_back(EV.workspace.active.listen([](PHLWORKSPACE) { scheduleChanged(); }));
    g_changeListeners.emplace_back(EV.workspace.created.listen([](PHLWORKSPACEREF) { scheduleChanged(); }));
    g_changeListeners.emplace_back(EV.workspace.removed.listen([](PHLWORKSPACEREF) { scheduleChanged(); }));
    g_changeListeners.emplace_back(EV.workspace.moveToMonitor.listen([](PHLWORKSPACE, PHLMONITOR) { scheduleChanged(); }));
    g_changeListeners.emplace_back(EV.window.open.listen([](PHLWINDOW) { scheduleChanged(); }));
    g_changeListeners.emplace_back(EV.window.destroy.listen([](PHLWINDOWREF) { scheduleChanged(); }));
    g_changeListeners.emplace_back(EV.window.moveToWorkspace.listen([](PHLWINDOW, PHLWORKSPACE) { scheduleChanged(); }));
    g_changeListeners.emplace_back(EV.monitor.focused.listen([](PHLMONITOR) { scheduleChanged(); }));

    // row_mode is the state at startup: apply it once, on the first config (re)load
    static bool rowModeApplied = false;
    g_reloadListener           = Event::bus()->m_events.config.reloaded.listen([] {
        if (rowModeApplied)
            return;
        rowModeApplied    = true;
        g_state.m_rowMode = Cfg::rowModeAtStart();
        scheduleChanged();
    });

    g_state.seedFromMonitors();
    g_state.m_rowMode = Cfg::rowModeAtStart();
    scheduleChanged();

    return {"hyprsubs", "2D workspaces: groups (horizontal) with subs (vertical)", "0TrashPanda", "0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_activeListener.reset();
    g_reloadListener.reset();
    g_changeListeners.clear();
    g_emitLater.reset();

    if (g_hyprctlCommand)
        HyprlandAPI::unregisterHyprCtlCommand(PHANDLE, g_hyprctlCommand);

    g_pSubSwipe.reset();
}
