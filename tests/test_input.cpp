// Fake-runtime regression seam linked against the actual core/input.cpp.
#include "../src/core/input.h"
#include "../src/core/vr_trace.h"
#include "../src/core/diagnostics.h"
#include "../src/core/combat_native.h"
#include "../src/core/arm_ik.h"
#include "../src/common/logging.h"
#include <openxr/openxr.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
// input.cpp alone is linked here; the camera gameplay gate is stubbed so the
// test can pin both the front-end (menu) and gameplay pad mappings.
namespace dsvr {
bool g_camera_gameplay_ready = true;
bool CameraGameplayReady() { return g_camera_gameplay_ready; }
// Settable native equipment identity, so a test can swap weapons mid-swing.
CombatNativeStatus g_combat_status{};
std::vector<std::string> BridgeLogLines();
void BridgeLogClear();
}


namespace {
const float kHead[3] = {0.0f, 1.6f, 0.0f};
std::uintptr_t next_handle = 1;
XrPath left_path = XR_NULL_PATH;
XrAction stick_action = XR_NULL_HANDLE;

XrAction aim_action = XR_NULL_HANDLE;
XrAction squeeze_action = XR_NULL_HANDLE;
XrAction trigger_action = XR_NULL_HANDLE;
XrAction menu_action = XR_NULL_HANDLE;
XrAction primary_action = XR_NULL_HANDLE;
XrAction secondary_action = XR_NULL_HANDLE;
XrAction stick_click_action = XR_NULL_HANDLE;
XrSpace view_space = XR_NULL_HANDLE;
XrResult turn_result = XR_SUCCESS;
XrResult sync_result = XR_SUCCESS;
int right_stick_queries = 0;
int fail_right_query = 0;
dsvr::Config config{};
int failures = 0;
bool calibration_capture = false;
bool menu_capture = false;
bool menu_held = false;
float right_squeeze = 0;
float right_trigger = 0;
float right_stick_value = 1;
int menu_toggles = 0;
float menu_x = 0;
float menu_y = 0;
bool menu_selected = false;
bool menu_back = false;
bool menu_prev = false;
bool menu_next = false;
bool right_primary = false;
std::vector<std::string> bridge_log_lines;
bool right_secondary = false;
bool left_secondary = false;
bool left_primary = false;
int theater_toggles = 0;
bool left_stick_click = false;
bool right_stick_click = false;
bool left_pose_valid = true;
bool right_pose_valid = true;
float grip_position[2][3]{};
bool applied_hands_valid = false;
std::uint64_t applied_hands_epoch = 1;
bool aim_pose_valid = true;
bool aim_active = true;
float aim_quat[4] = {.258819045f,0,0,.965925826f};
float head_quat[4] = {0, 0, 0, 1};
float left_quat[4] = {0, 0, 0, 1};
float right_quat[4] = {0, 0, 0, 1};
int calibration_requests = 0;
int recenter_calls = 0;
bool sanctuary = false;
bool trace_active = false;
std::uint64_t trace_capture_id = 1;
std::vector<dsvr::VrTraceEvent> trace_events;
float left_stick_x = 0;
float left_stick_y = 0;
std::map<std::string, XrPath> paths;
std::map<XrPath, std::string> path_names;
std::map<std::string, std::vector<XrActionSuggestedBinding>> suggested_bindings;
std::map<std::string, int> suggestion_counts;
// A live binding rebuild resolves its teardown through the instance, exactly as
// a runtime does, so the fake hands back real destroy entry points and counts
// them: the rebuild path is then exercised instead of skipped.
int destroyed_spaces = 0, destroyed_sets = 0;

template <typename T> T Handle() {
    return reinterpret_cast<T>(next_handle++);
}

void Expect(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void ProfileBindings() {
    // OpenXR replaces a profile's previous successful suggestion; it does not
    // append an extra menu/click batch to the existing pose/stick bindings.
    for (const char* profile : {
             "/interaction_profiles/oculus/touch_controller",
             "/interaction_profiles/valve/index_controller",
             "/interaction_profiles/microsoft/motion_controller"}) {
        const auto& bindings = suggested_bindings[profile];
        Expect(suggestion_counts[profile] == 1,
               "each controller profile submits one complete binding set");
        for (const char* action_path : {
                 "/user/hand/left/input/grip/pose",
                 "/user/hand/right/input/grip/pose",
                 "/user/hand/left/input/aim/pose",
                 "/user/hand/right/input/aim/pose",
                 "/user/hand/left/input/trigger/value",
                 "/user/hand/right/input/trigger/value",
                 "/user/hand/left/input/thumbstick",
                 "/user/hand/right/input/thumbstick",
                 "/user/hand/left/input/thumbstick/click",
                 "/user/hand/right/input/thumbstick/click",
                 "/user/hand/left/output/haptic",
                 "/user/hand/right/output/haptic"}) {
            bool found = false;
            for (const auto& binding : bindings)
                found |= path_names[binding.binding] == action_path;
            Expect(found, "menu/click bindings retain pose, trigger, stick and haptics");
        }
        bool menu = false;
        int squeezes = 0;
        for (const auto& binding : bindings) menu |= binding.action == menu_action;
        const bool motion = std::strcmp(profile,
            "/interaction_profiles/microsoft/motion_controller") == 0;
        for (const auto& binding : bindings) {
            const auto& path = path_names[binding.binding];
            squeezes += path.ends_with(motion ? "/input/squeeze/click"
                                              : "/input/squeeze/value");
        }
        Expect(squeezes == 2, "squeeze bindings use each profile's supported component");
        Expect(menu, "each full controller profile retains its menu binding");
        if (std::strcmp(profile, "/interaction_profiles/oculus/touch_controller") == 0)
            Expect(bindings.size() == 19,
                   "Touch accepts its entire supported profile without phantom directions");
    }
}

void ResetRoomscaleTestControls();
void DeferredBindingEdits() {
    const auto base = reinterpret_cast<XrSpace>(100);
    const char* quest = "/interaction_profiles/oculus/touch_controller";
    const char* motion = "/interaction_profiles/microsoft/motion_controller";
    const int sets = destroyed_sets, spaces = destroyed_spaces;
    const int quest_before = suggestion_counts[quest];
    const int motion_before = suggestion_counts[motion];
    const auto saved = config;
    ResetRoomscaleTestControls();
    dsvr::InputPollFrame(base, kHead, 1, 3200000000LL);
    right_primary = true;
    dsvr::InputPollFrame(base, kHead, 1, 3210000000LL);
    dsvr::VirtualPadState before{};
    Expect(dsvr::InputLatestPad(before) && (before.buttons & dsvr::kPadA),
           "the held A is already reaching the game before a binding edit");
    config.binding_live_reattach = true;
    config.bind_profile_wmr = false;
    right_primary = true;
    dsvr::InputRequestBindingReload();
    dsvr::InputPollFrame(base, kHead, 1, 3220000000LL);
    dsvr::VirtualPadState pad{};
    Expect(dsvr::InputAvailable() && dsvr::InputLatestPad(pad) &&
           (pad.buttons & dsvr::kPadA),
           "binding edits preserve the live controller and its current A press");
    Expect(destroyed_sets == sets && destroyed_spaces == spaces,
           "an immutable attached set and its spaces are never destroyed for reload");
    Expect(suggestion_counts[quest] == quest_before &&
           suggestion_counts[motion] == motion_before,
           "profile edits are deferred instead of re-suggested or re-attached");
    dsvr::InputPollFrame(base, kHead, 1, 3230000000LL);
    Expect(dsvr::InputAvailable(), "deferred changes do not break subsequent input polls");
    config.binding_live_reattach = false;
    dsvr::InputRequestBindingReload();
    dsvr::InputPollFrame(base, kHead, 1, 3240000000LL);
    Expect(destroyed_sets == sets && dsvr::InputAvailable(),
           "both legacy reload preferences preserve the active session");
    right_primary = false;
    config = saved;
}

void TheaterButtons() {
    using namespace dsvr;
    const auto base = reinterpret_cast<XrSpace>(100);
    const auto poll = [&](long long time) {
        InputPublishHeadDirectedLocomotion({0, 0, 1}, {0, 0, 1}, 1);
        InputPollFrame(base, kHead, 1, time);
        VirtualPadState state{};
        InputLatestPad(state);
        return state.buttons;
    };
    InputInvalidateFrame();
    right_stick_value = 0;
    right_secondary = true;
    Expect(!(poll(10000000000LL) & kPadB), "secondary press waits briefly for its chord partner");
    right_secondary = false;
    Expect(poll(10050000000LL) & kPadB, "a quick lone B tap is delivered on release");
    Expect(!(poll(10060000000LL) & kPadB), "the delayed B tap is delivered once");

    const int before = theater_toggles;
    left_secondary = true;
    Expect(!(poll(11000000000LL) & kPadY), "a possible chord withholds its first button");
    right_secondary = true;
    Expect(!(poll(11100000000LL) & (kPadY | kPadB)), "both chord buttons stay out of gameplay");
    poll(11600000000LL);
    Expect(theater_toggles == before + 1, "holding both secondary buttons toggles theater once");
    right_secondary = false;
    Expect(!(poll(11700000000LL) & kPadY), "releasing half a consumed chord cannot emit Y");
    right_secondary = true;
    poll(11800000000LL); poll(12400000000LL);
    Expect(theater_toggles == before + 1, "a chord rearms only after both buttons release");
    left_secondary = right_secondary = false;
    poll(12500000000LL);

    left_secondary = right_secondary = true;
    poll(13000000000LL);
    InputInvalidateFrame();
    poll(14000000000LL);
    Expect(theater_toggles == before + 1, "tracking invalidation discards a partial chord hold");
    left_secondary = right_secondary = false;
    poll(14010000000LL);
    right_stick_value = 1;
    InputInvalidateFrame();
}

void FailedTurnQuery(bool smooth) {
    config.snap_turn = !smooth;
    config.smooth_turn = smooth;
    config.snap_turn_degrees = 45;
    config.smooth_turn_speed_dps = 180.0f;
    turn_result = XR_SUCCESS;
    dsvr::InputInvalidateFrame();

    const XrSpace base = reinterpret_cast<XrSpace>(100);
    dsvr::InputPollFrame(base, kHead, 1, 1000000000);
    dsvr::InputPollFrame(base, kHead, 1, 1010000000);
    Expect(dsvr::InputTakeTurnYaw() > 0.0f,
           "successful right-stick queries publish turn yaw");
    Expect(dsvr::InputLatestComfortMotion().turn_radians > 0,
           "comfort receives emitted snap and smooth turns");

    dsvr::InputInvalidateFrame();
    dsvr::InputPollFrame(base, kHead, 1, 1000000000);
    dsvr::InputPollFrame(base, kHead, 1, 1010000000);
    // Leave the successful frame's yaw undrained, then fail the right-stick query.
    turn_result = XR_ERROR_RUNTIME_FAILURE;
    dsvr::InputPollFrame(base, kHead, 1, 1020000000);
    float yaw = dsvr::InputTakeTurnYaw();
    Expect(dsvr::InputLatestComfortMotion().turn_radians == 0,
           "failed right-stick queries clear comfort turn input");
    if (yaw != 0.0f) {
        std::fprintf(stderr, "FAIL: %s turn query failure retained %.6f radians of stale yaw\n",
                     smooth ? "smooth" : "snap", yaw);
        ++failures;
    }

    // Once query failure discards a snap, recovery must not retain its latch.
    turn_result = XR_SUCCESS;
    dsvr::InputPollFrame(base, kHead, 1, 1030000000);
    yaw = dsvr::InputTakeTurnYaw();
    if (smooth) {
        Expect(yaw > 0.0f, "smooth turning resumes after a failed query");
    } else {
        Expect(std::fabs(yaw - 0.785398163f) < 0.00001f,
               "a fresh successful sample can snap after a failed query");
    }
}

void FailedFirstTurnQuery(bool smooth) {
    config.snap_turn = !smooth;
    config.smooth_turn = smooth;
    turn_result = XR_SUCCESS;
    dsvr::InputInvalidateFrame();
    const XrSpace base = reinterpret_cast<XrSpace>(100);
    dsvr::InputPollFrame(base, kHead, 1, 1000000000);
    dsvr::InputPollFrame(base, kHead, 1, 1010000000);

    // Fail just the first right-stick query after sync. A second query must
    // not hide that error or retain the previous frame's queued turn.
    right_stick_queries = 0;
    fail_right_query = 1;
    dsvr::InputPollFrame(base, kHead, 1, 1020000000);
    Expect(dsvr::InputTakeTurnYaw() == 0.0f,
           "a failed first right-stick query discards queued turn yaw");
    Expect(right_stick_queries == 1,
           "pad and turning share one right-stick sample per frame");
    fail_right_query = 0;
    dsvr::InputPollFrame(base, kHead, 1, 1030000000);
    Expect(dsvr::InputTakeTurnYaw() > 0.0f,
           "turning resumes after the failed first right-stick query");
}

void ResetRoomscaleTestControls() {
    right_stick_value = 0;
    right_squeeze = 0;
    right_trigger = 0;
    right_primary = false;
    right_secondary = false;
    menu_held = false;
    menu_capture = false;
    calibration_capture = false;
    sanctuary = false;
    left_stick_x = 0;
    left_stick_y = 0;
    left_stick_click = right_stick_click = false;
     left_primary = false;
}
void DigitalDpad() {
    using namespace dsvr;
    const auto base = reinterpret_cast<XrSpace>(100);
    ResetRoomscaleTestControls();
    InputInvalidateFrame();
    long long time = 17000000000LL;
    VirtualPadState pad{};
    const auto poll = [&] {
        InputPublishHeadDirectedLocomotion({0, 0, 1}, {0, 0, 1}, 1);
        InputPollFrame(base, kHead, 1, time);
        time += 10000000LL;
        Expect(InputLatestPad(pad), "valid actions keep the virtual controller available");
    };
    left_stick_y = 0.9f;
    poll();
    Expect(pad.thumb_ly > 0 && !(pad.buttons & kPadDpadUp),
           "ordinary stick movement walks without selecting equipment");
    left_stick_click = true;
    poll();
    Expect((pad.buttons & kPadDpadUp) && pad.thumb_ly == 0 &&
           !(pad.buttons & kPadLeftThumb),
           "click plus stick selects without walking or a native jump");
    left_stick_y = 0;
    left_stick_x = -1;
    poll();
    Expect((pad.buttons & kPadDpadLeft) && !(pad.buttons & kPadDpadUp),
           "changing direction replaces the previous D-pad press");
    left_stick_click = false;
    poll();
    Expect(pad.buttons == 0 && pad.thumb_lx == 0,
           "modifier release while deflected cannot lunge or jump");
    poll();
    Expect(pad.thumb_lx == 0, "movement stays disarmed until stick centre");
    left_stick_x = 0;
    poll();
    left_stick_y = 0.9f;
    poll();
    Expect(pad.thumb_ly > 0, "fresh deflection walks after returning to centre");
    left_stick_y = 0;
    left_stick_click = true;
    poll();
    Expect(!(pad.buttons & kPadLeftThumb), "neutral click cannot jump early");
    left_stick_click = false;
    poll();
    Expect(pad.buttons & kPadLeftThumb, "neutral lone click preserves L3 on release");
    poll();
    Expect(pad.buttons & kPadLeftThumb, "deferred L3 survives slower native polls");
    time += 120000000LL;
    poll();
    Expect(!(pad.buttons & kPadLeftThumb), "deferred L3 releases after its bounded hold");
    g_camera_gameplay_ready = false;
    left_stick_x = 0.9f;
    poll();
    Expect((pad.buttons & kPadDpadRight) && pad.thumb_lx == 0,
           "native menus navigate without a modifier");
    g_camera_gameplay_ready = true;
    menu_capture = true;
    left_primary = true;
    poll();
    Expect(menu_x == 0.9f && menu_y == 0 && menu_prev && !menu_next,
           "spatial menus keep stick navigation and X panel selection");
    left_primary = false;
    left_secondary = true;
    poll();
    Expect(menu_next && !menu_prev, "Y selects the next spatial panel");
    left_secondary = false;
    left_stick_click = true;
    poll();
    menu_capture = false;
    poll();
    Expect(pad.buttons == 0 && pad.thumb_lx == 0,
           "held menu navigation and modifier cannot leak into gameplay");
    left_stick_click = false;
    left_stick_x = 0;
    poll();
    left_pose_valid = false;
    left_stick_click = true;
    left_stick_y = -1;
    poll();
    Expect((pad.buttons & kPadDpadDown) && pad.thumb_ly == 0,
           "D-pad remains usable when grip tracking is lost");
    InputInvalidateFrame();
    left_pose_valid = true;
    left_stick_click = false;
    left_stick_y = 0;
    poll();
    Expect(pad.buttons == 0, "invalidation cannot synthesize a pending L3 click");
    ResetRoomscaleTestControls();
    InputInvalidateFrame();
}
// Observe the real poll/publish seam, including the existing deferred B tap.
// This establishes analog input delivery, not the native roll motor's direction.
void AnalogDodgeRequest() {
    using namespace dsvr;
    const auto saved = config;
    ResetRoomscaleTestControls();
    InputInvalidateFrame();
    config.movement_orientation = MovementOrientation::Head;
    config.play_style = "seated";
    long long time = 47000000000LL;
    VirtualPadState pad{};
    const auto poll = [&] {
        InputPublishHeadDirectedLocomotion({0, 0, 1}, {0, 0, 1}, 1);
        InputPollFrame(reinterpret_cast<XrSpace>(100), kHead, 1, time);
        time += 10000000LL;
        Expect(InputLatestPad(pad), "dodge request publishes a current pad");
    };
    poll();
    for (float angle : {0.29f, 0.71f, 2.21f, 3.89f, 5.57f}) {
        left_stick_x = 0.8f * std::cos(angle);
        left_stick_y = 0.8f * std::sin(angle);
        // The theater chord reserves B initially; a lone quick tap is emitted
        // on release, then releases on the following poll.
        for (int stage = 0; stage < 3; ++stage) {
            right_secondary = stage == 0;
            poll();
            Expect(bool(pad.buttons & kPadB) == (stage == 1),
                   "deferred dodge B tap and release reach the native input seam");
            const float delivered = std::atan2(float(pad.thumb_ly), float(pad.thumb_lx));
            Expect(std::fabs(std::sin(delivered - angle)) < 0.001f &&
                   std::cos(delivered - angle) > 0,
                   "non-cardinal dodge intent retains its continuous direction");
        }
    }
    config = saved;
    ResetRoomscaleTestControls();
    InputInvalidateFrame();
}
void HeadDirectedLocomotion() {
    const XrSpace base = reinterpret_cast<XrSpace>(100);
    dsvr::VirtualPadState pad{};
    config.movement_orientation = dsvr::MovementOrientation::Head;
    sanctuary = false;
    left_stick_x = 0;
    left_stick_y = 1;
    right_stick_value = 0;
    dsvr::InputInvalidateFrame();
    dsvr::InputPublishHeadDirectedLocomotion({1, 0, 0}, {0, 0, 1}, 1);
    dsvr::InputPollFrame(base, kHead, 1, 1800000000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_lx > 32000 && pad.thumb_ly == 0,
           "head orientation rotates forward stick into the native movement frame");

    left_stick_y = 0;
    config.movement_orientation = dsvr::MovementOrientation::Controller;
    config.movement_controller = dsvr::MovementController::Left;
    left_pose_valid = true;
    dsvr::InputPollFrame(base, kHead, 1, 1805000000);
    left_pose_valid = false;
    left_stick_y = 1;
    dsvr::InputPollFrame(base, kHead, 1, 1810000000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_lx > 32000 && pad.thumb_ly == 0,
           "controller mode steers by the head when its selected hand is untracked");
    left_pose_valid = true;
    config.movement_orientation = dsvr::MovementOrientation::Head;

    dsvr::InputPublishHeadDirectedLocomotion({1, 0, 0}, {0, 0, 1}, 2);
    dsvr::InputPollFrame(base, kHead, 1, 1820000000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_ly > 32000 && pad.thumb_lx > -2000 &&
           pad.thumb_lx < 2000,
           "a locomotion basis from another tracking epoch keeps the raw stick");

    dsvr::InputPublishHeadDirectedLocomotion({1, 0, 0}, {0, 0, 1}, 1);
    sanctuary = true;
    dsvr::InputPollFrame(base, kHead, 1, 1830000000);
    Expect(dsvr::InputLatestPad(pad) && (pad.buttons & dsvr::kPadDpadUp) &&
           !(pad.buttons & dsvr::kPadDpadRight),
           "sanctuary navigation keeps raw stick direction");

    sanctuary = false;
    dsvr::InputInvalidateHeadDirectedLocomotion();
    dsvr::InputPollFrame(base, kHead, 1, 1840000000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_ly > 32000 && pad.thumb_lx > -2000 &&
           pad.thumb_lx < 2000,
           "an invalidated locomotion basis keeps the raw stick, never silence");

    // The reported failure: look, arms and the camera all worked while the
    // player could neither move nor select menu options. Every cause of that
    // is a missing/stale camera basis, and the old fallback deleted the left
    // stick. A full forward push with no basis at all must still walk, and a
    // menu must still take the d-pad, because neither needs an orientation.
    dsvr::InputInvalidateFrame();
    left_stick_x = 0;
    left_stick_y = 1;
    dsvr::InputPollFrame(base, kHead, 1, 1845000000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_ly > 32000 && pad.thumb_lx < 2000 &&
           pad.thumb_lx > -2000,
           "no locomotion basis at all still walks forward");
    dsvr::g_camera_gameplay_ready = false;
    dsvr::InputPollFrame(base, kHead, 1, 1850000000);
    Expect(dsvr::InputLatestPad(pad) && (pad.buttons & dsvr::kPadDpadUp) &&
           !(pad.buttons & dsvr::kPadDpadRight),
           "menu navigation survives a missing locomotion basis");
    dsvr::g_camera_gameplay_ready = true;
    ResetRoomscaleTestControls();
    config.vr_enabled = true;
    config.headlook_enabled = true;
    config.play_style = "roomscale";
    dsvr::InputInvalidateFrame();
    dsvr::RoomscaleBasis room_basis{};
    room_basis.tracking_right = {1, 0, 0};
    room_basis.tracking_up = {0, 1, 0};
    room_basis.tracking_forward = {0, 0, 1};
    room_basis.movement_basis = {0, 0, 1};
    room_basis.space_epoch = 1;
    room_basis.valid = true;
    dsvr::InputPublishCameraMotionBasis(room_basis, {0, 0, 1}, {0, 0, 1}, 1);
    const float room_start[3] = {0.0f, 1.6f, 0.0f};
    const float room_forward[3] = {0.0f, 1.6f, -0.2f};
    dsvr::InputPollFrame(base, room_start, 1, 1900000000);
    left_stick_x = 0.5f;
    dsvr::InputPollFrame(base, room_start, 1, 1925000000);
    dsvr::InputPollFrame(base, room_forward, 1, 1950000000);
    dsvr::VirtualPadState room_pad{};
    Expect(dsvr::InputLatestPad(room_pad) && room_pad.thumb_lx > 0 &&
           room_pad.thumb_ly > 0,
           "physical walking remains sampled and composes with simultaneous stick translation");
    dsvr::RoomscaleMotion motion{};
    Expect(dsvr::InputLatestRoomscaleMotion(motion) && motion.active &&
           motion.calibration_revision == room_basis.calibration_revision &&
           motion.world_scale == room_basis.world_scale,
           "roomscale motion carries applied-camera generation metadata");
    room_basis.calibration_revision++;
    dsvr::InputPublishCameraMotionBasis(room_basis, {0, 0, 1}, {0, 0, 1}, 1);
    Expect(!dsvr::InputLatestRoomscaleMotion(motion), "new calibration cannot expose prior-generation motion");
    dsvr::InputInvalidateFrame();
}

void MovementReferenceFrames() {
    const XrSpace base = reinterpret_cast<XrSpace>(100);
    dsvr::VirtualPadState pad{};
    Expect(config.movement_orientation == dsvr::MovementOrientation::Head,
           "head is the default movement orientation");
    config.movement_orientation = dsvr::MovementOrientation::Controller;
    config.movement_controller = dsvr::MovementController::Left;
    sanctuary = false;
    left_stick_x = left_stick_y = 0;
    right_stick_value = 0;
    head_quat[0] = head_quat[1] = head_quat[2] = 0;
    head_quat[3] = 1;
    left_quat[0] = left_quat[2] = 0;
    left_quat[1] = 0.70710678f;
    left_quat[3] = 0.70710678f;
    right_quat[0] = right_quat[1] = right_quat[2] = 0;
    right_quat[3] = 1;
    left_pose_valid = true;
    dsvr::RoomscaleBasis basis{};
    basis.tracking_right = {1, 0, 0};
    basis.tracking_up = {0, 1, 0};
    basis.tracking_forward = {0, 0, 1};
    basis.movement_basis = {0, 0, 1};
    basis.space_epoch = 1;
    basis.valid = true;
    dsvr::InputPublishRoomscaleBasis(basis);
    dsvr::InputPublishHeadDirectedLocomotion({0, 0, 1}, {0, 0, 1}, 1);
    dsvr::InputPollFrame(base, kHead, 1, 2390000000);
    left_stick_y = 1;
    dsvr::InputPollFrame(base, kHead, 1, 2400000000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_lx < -32000 &&
           std::abs(pad.thumb_ly) < 2,
           "selected left controller heading steers controller-relative movement");
    const auto controller_heading_x = pad.thumb_lx;
    head_quat[1] = 0.70710678f;
    head_quat[3] = 0.70710678f;
    dsvr::InputPublishHeadDirectedLocomotion({1, 0, 0}, {0, 0, 1}, 1);
    dsvr::InputPollFrame(base, kHead, 1, 2405000000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_lx == controller_heading_x &&
           std::abs(pad.thumb_ly) < 2,
           "head scanning does not change travel direction with a fixed controller heading");
    head_quat[1] = 0.0f;
    head_quat[3] = 1.0f;
    left_quat[0] = 0.70710678f;
    left_quat[1] = left_quat[2] = 0.0f;
    left_quat[3] = 0.70710678f;
    dsvr::InputPollFrame(base, kHead, 1, 2406000000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_lx == controller_heading_x &&
           std::abs(pad.thumb_ly) < 2,
           "near-vertical controller aim retains the last stable planar heading");
    left_pose_valid = false;
    dsvr::InputPollFrame(base, kHead, 1, 2406500000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_lx > 32000 && std::abs(pad.thumb_ly) < 2,
           "controller tracking loss returns to head-oriented movement, not a dead stick");
    left_pose_valid = true;
    left_quat[0] = 0.0f;
    left_quat[1] = 0.70710678f;
    left_quat[3] = 0.70710678f;
    dsvr::InputPollFrame(base, kHead, 1, 2406800000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_lx > 32000 && std::abs(pad.thumb_ly) < 2,
           "reacquisition stays head-oriented until a neutral stick re-arms the hand heading");
    left_stick_y = 0;
    dsvr::InputPollFrame(base, kHead, 1, 2407000000);
    left_stick_y = 1;
    dsvr::InputPollFrame(base, kHead, 1, 2407100000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_lx == controller_heading_x,
           "controller movement resumes after valid heading and neutral rearm");
    Expect(dsvr::InputTakeTurnYaw() == 0,
           "controller-relative movement changes locomotion only, not turn yaw");

    left_stick_y = 0;
    dsvr::InputPollFrame(base, kHead, 1, 2407000000);
    config.movement_controller = dsvr::MovementController::Right;
    dsvr::InputPollFrame(base, kHead, 1, 2408000000);
    right_quat[1] = -0.70710678f;
    right_quat[3] = 0.70710678f;
    left_stick_y = 1;
    dsvr::InputPollFrame(base, kHead, 1, 2410000000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_lx > 32000 &&
           std::abs(pad.thumb_ly) < 2,
           "explicit right controller reference steers movement after neutral release");

    left_stick_y = 0;
    dsvr::InputPollFrame(base, kHead, 1, 2415000000);
    config.movement_controller = dsvr::MovementController::NonDominant;
    config.dominant_hand_left = false;
    dsvr::InputPollFrame(base, kHead, 1, 2416000000);
    left_stick_y = 1;
    dsvr::InputPollFrame(base, kHead, 1, 2417000000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_lx < -32000,
           "non-dominant reference resolves to left controller for right-handed mode");
    left_stick_y = 0;
    dsvr::InputPollFrame(base, kHead, 1, 2418000000);
    config.dominant_hand_left = true;
    dsvr::InputPollFrame(base, kHead, 1, 2419000000);
    left_stick_y = 1;
    dsvr::InputPollFrame(base, kHead, 1, 2420000000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_lx > 32000,
           "non-dominant reference resolves to right controller after neutral hand change");

    left_stick_y = 0;
    dsvr::InputPollFrame(base, kHead, 1, 2421000000);
    config.movement_controller = dsvr::MovementController::Left;
    left_pose_valid = false;
    dsvr::InputPollFrame(base, kHead, 1, 2422000000);
    left_stick_y = 1;
    dsvr::InputPollFrame(base, kHead, 1, 2423000000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_lx > 32000 && pad.thumb_ly == 0,
           "a lost selected controller never deadens locomotion: the head reference takes over");
    left_pose_valid = true;
    left_stick_x = left_stick_y = 0;
    config.movement_orientation = dsvr::MovementOrientation::Head;
    config.dominant_hand_left = false;
    dsvr::InputPollFrame(base, kHead, 1, 2425000000);
}

// The finished locomotion contract: the left stick always has exactly one live
// orientation reference. Head is the default and needs no controller; either
// hand can be selected explicitly; a never-tracked or lost hand hands steering
// back to the head instead of deadening smooth locomotion.
void MovementAlwaysOriented() {
    const auto saved = config;
    const XrSpace base = reinterpret_cast<XrSpace>(100);
    dsvr::VirtualPadState pad{};
    ResetRoomscaleTestControls();
    left_pose_valid = right_pose_valid = false;
    head_quat[0] = head_quat[1] = head_quat[2] = 0;
    head_quat[3] = 1;
    left_quat[0] = left_quat[1] = left_quat[2] = 0;
    left_quat[3] = 1;
    right_quat[0] = right_quat[1] = right_quat[2] = 0;
    right_quat[3] = 1;
    dsvr::InputInvalidateFrame();
    dsvr::RoomscaleBasis basis{};
    basis.tracking_right = {1, 0, 0};
    basis.tracking_up = {0, 1, 0};
    basis.tracking_forward = {0, 0, 1};
    basis.movement_basis = {0, 0, 1};
    basis.space_epoch = 5;
    dsvr::InputPublishRoomscaleBasis(basis);
    dsvr::InputPublishHeadDirectedLocomotion({0, 0, 1}, {0, 0, 1}, 5);
    // Settings only latch while the stick is neutral, so every mode change here
    // goes through a neutral frame first.
    config.movement_orientation = dsvr::MovementOrientation::Head;
    config.movement_controller = dsvr::MovementController::Right;
    dsvr::InputPollFrame(base, kHead, 5, 2500000000);
    left_stick_y = 1;
    dsvr::InputPollFrame(base, kHead, 5, 2501000000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_ly > 32000 && pad.thumb_lx == 0,
           "default head-oriented smooth locomotion needs no controller tracked");

    config.movement_orientation = dsvr::MovementOrientation::Controller;
    left_stick_y = 0;
    dsvr::InputPollFrame(base, kHead, 5, 2502000000);
    left_stick_y = 1;
    dsvr::InputPollFrame(base, kHead, 5, 2503000000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_ly > 32000 && pad.thumb_lx == 0,
           "a never-tracked selected hand falls back to the head reference");

    right_pose_valid = true;
    right_quat[1] = 0.70710678f;
    right_quat[3] = 0.70710678f;
    left_stick_y = 0;
    dsvr::InputPollFrame(base, kHead, 5, 2504000000);
    left_stick_y = 1;
    dsvr::InputPollFrame(base, kHead, 5, 2505000000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_lx < -32000 && std::abs(pad.thumb_ly) < 2,
           "the reacquired right hand steers after a neutral rearm");

    right_pose_valid = false;
    dsvr::InputPollFrame(base, kHead, 5, 2506000000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_ly > 32000 && pad.thumb_lx == 0,
           "losing the steering hand mid-stride returns to head-oriented movement");

    right_pose_valid = true;
    right_quat[1] = 0;
    right_quat[3] = 1;
    left_stick_y = 0;
    config.movement_orientation = dsvr::MovementOrientation::Head;
    config.movement_controller = dsvr::MovementController::NonDominant;
    dsvr::InputPollFrame(base, kHead, 5, 2507000000);
    dsvr::InputInvalidateFrame();
    config = saved;
}

void ReferenceChords() {
    const XrSpace base = reinterpret_cast<XrSpace>(100);
    dsvr::VirtualPadState pad{};
    menu_capture = false;
    calibration_capture = false;
    left_stick_x = left_stick_y = 0;
    right_stick_value = 0;
    right_squeeze = right_trigger = 0;
    right_primary = right_secondary = false;
    left_stick_click = right_stick_click = false;
    dsvr::InputPollFrame(base, kHead, 1, 4000000000);

    left_stick_click = right_stick_click = true;
    dsvr::InputPollFrame(base, kHead, 1, 5000000000);
    dsvr::InputPollFrame(base, kHead, 1, 7999999999);
    Expect(menu_toggles == 0 && dsvr::InputLatestPad(pad) && pad.buttons == 0,
           "both stick clicks are consumed and do not toggle before three seconds");
    dsvr::InputPollFrame(base, kHead, 1, 8000000000);
    dsvr::InputPollFrame(base, kHead, 1, 9000000000);
    Expect(menu_toggles == 1 && menu_capture,
           "the three-second stick-click chord toggles the menu once while held");
    Expect(dsvr::InputLatestPad(pad) && pad.buttons == 0,
           "menu chord stick-clicks never leak as native thumb buttons");
    right_stick_click = false;
    dsvr::InputPollFrame(base, kHead, 1, 9010000000);
    right_stick_click = true;
    dsvr::InputPollFrame(base, kHead, 1, 9020000000);
    dsvr::InputPollFrame(base, kHead, 1, 12020000000);
    Expect(menu_toggles == 1,
           "partial stick-click release cannot rearm a fired menu chord");
    left_stick_click = right_stick_click = false;
    dsvr::InputPollFrame(base, kHead, 1, 12030000000);
    menu_capture = false;
    dsvr::InputPollFrame(base, kHead, 1, 12040000000);

    left_stick_click = right_stick_click = true;
    dsvr::InputPollFrame(base, kHead, 1, 13000000000);
    dsvr::InputPollFrame(base, kHead, 1, 16000000000);
    Expect(menu_toggles == 2,
           "the menu chord re-arms only after a neutral release and a new hold");
    left_stick_click = right_stick_click = false;
    dsvr::InputPollFrame(base, kHead, 1, 13010000000);
    menu_capture = false;
    dsvr::InputPollFrame(base, kHead, 1, 13020000000);

    const int recenter_before = recenter_calls;
    left_stick_click = right_stick_click = true;
    dsvr::InputPollFrame(base, kHead, 1, 14000000000);
    left_stick_click = right_stick_click = false;
    dsvr::InputPollFrame(base, kHead, 1, 14100000000);
    Expect(recenter_calls == recenter_before + 1,
           "a short simultaneous click still recenters instead of opening the menu");
    dsvr::InputPollFrame(base, kHead, 1, 14110000000);

    const int request_before = calibration_requests;
    left_stick_click = true;
    right_primary = true;
    dsvr::InputPollFrame(base, kHead, 1, 15000000000);
    dsvr::InputPollFrame(base, kHead, 1, 15020000000);
    Expect(calibration_requests == request_before &&
           dsvr::InputLatestPad(pad) && pad.buttons == 0,
           "the first held A is consumed but cannot repeatedly trigger recalibration");
    right_primary = false;
    dsvr::InputPollFrame(base, kHead, 1, 15030000000);
    right_primary = true;
    dsvr::InputPollFrame(base, kHead, 1, 15050000000);
    Expect(calibration_requests == request_before,
           "a sub-debounce A edge is ignored instead of falsely recalibrating");
    right_primary = false;
    dsvr::InputPollFrame(base, kHead, 1, 15060000000);
    right_primary = true;
    dsvr::InputPollFrame(base, kHead, 1, 15500000000);
    dsvr::InputPollFrame(base, kHead, 1, 15600000000);
    Expect(calibration_requests == request_before + 1 &&
           dsvr::InputLatestPad(pad) && pad.buttons == 0,
           "left-click plus a debounced double-A requests recalibration and consumes A");
    right_primary = false;
    left_stick_click = false;
    calibration_capture = false;
    dsvr::InputPollFrame(base, kHead, 1, 15610000000);
    dsvr::InputPollFrame(base, kHead, 1, 15620000000);
    left_stick_click = true;
    right_primary = true;
    dsvr::InputPollFrame(base, kHead, 1, 16000000000);
    right_primary = false;
    dsvr::InputPollFrame(base, kHead, 1, 16010000000);
    right_primary = true;
    dsvr::InputPollFrame(base, kHead, 1, 16300000000);
    Expect(calibration_requests == request_before + 2,
           "the recalibration chord re-arms after all controls return neutral");
    left_stick_click = false;
    dsvr::InputPollFrame(base, kHead, 1, 16310000000);
    left_stick_click = true;
    dsvr::InputPollFrame(base, kHead, 1, 16320000000);
    right_primary = false;
    dsvr::InputPollFrame(base, kHead, 1, 16330000000);
    right_primary = true;
    dsvr::InputPollFrame(base, kHead, 1, 16500000000);
    right_primary = false;
    dsvr::InputPollFrame(base, kHead, 1, 16510000000);
    right_primary = true;
    dsvr::InputPollFrame(base, kHead, 1, 16810000000);
    Expect(calibration_requests == request_before + 2,
           "partial left-click release cannot rearm a fired recalibration chord");
    right_primary = false;
    left_stick_click = false;
    calibration_capture = false;
    dsvr::InputPollFrame(base, kHead, 1, 16820000000);
    dsvr::InputPollFrame(base, kHead, 1, 16830000000);
    menu_toggles = 0;
}

void CalibratedRoomscale() {
    const auto saved=config;
    config.vr_enabled=config.headlook_enabled=true;
    config.play_style="roomscale";config.world_scale=1.7f;
    menu_capture=calibration_capture=sanctuary=false;
    right_stick_value=left_stick_x=left_stick_y=right_trigger=right_squeeze=0;
    const XrSpace base=reinterpret_cast<XrSpace>(100);
    dsvr::InputInvalidateFrame();
    dsvr::InputPublishHeadDirectedLocomotion({0,0,1},{0,0,1},1);
    dsvr::RoomscaleBasis basis;
    basis.tracking_right={1,0,0};basis.tracking_up={0,1,0};
    basis.tracking_forward=basis.movement_basis={0,0,1};basis.space_epoch=1;
    basis.world_scale=.8f;basis.calibration_revision=7;
    dsvr::InputPublishRoomscaleBasis(basis);
    float head[3]{0,1.6f,0};long long time=18000000000;
    auto step=[&] {time+=10000000;dsvr::InputPollFrame(base,head,1,time);};
    step();head[0]+=.01f;step();head[0]+=.01f;step();
    dsvr::RoomscaleMotion motion;
    Expect(dsvr::InputLatestRoomscaleMotion(motion) &&
           std::fabs(motion.world_velocity.x-.8f)<1e-4f,
           "roomscale uses applied camera scale, not a fresh independent config gain");
    Expect(dsvr::InputLatestComfortMotion().move == 0,
           "physical follow cannot activate the comfort vignette");
    basis.world_scale=1.2f;dsvr::InputPublishRoomscaleBasis(basis);
    head[0]+=.01f;step();
    Expect(!dsvr::InputLatestRoomscaleMotion(motion),
           "world scale changes re-prime physical velocity without stored motion");
    head[0]+=.01f;step();
    Expect(dsvr::InputLatestRoomscaleMotion(motion) &&
           std::fabs(motion.world_velocity.x-1.2f)<1e-4f,
           "new coherent scale is consumed exactly once");
    ++basis.calibration_revision;dsvr::InputPublishRoomscaleBasis(basis);
    Expect(!dsvr::InputLatestRoomscaleMotion(motion),
           "recalibration clears old roomscale motion even at the same scale");
    basis.world_scale=NAN;dsvr::InputPublishRoomscaleBasis(basis);
    Expect(!dsvr::InputLatestRoomscaleMotion(motion),"invalid scale refuses roomscale");
    dsvr::InputInvalidateFrame();config=saved;
}

// The physical defence bridge: the shield hand's own tracked pose drives the
// native shield button. A raised, still hand holds it; a flick taps it. The
// test only ever looks at the published pad, so it proves the whole chain
// (pose -> guard state machine -> pad mapping -> XInput seam) rather than the
// state machine alone.
void PhysicalDefence() {
    using namespace dsvr;
    const auto saved = config;
    const bool saved_left_pose = left_pose_valid;
    const bool saved_right_pose = right_pose_valid;
    left_pose_valid = right_pose_valid = true;
    ResetRoomscaleTestControls();
    right_stick_value = 0;
    config.arm_ik_enabled = true;
    config.arm_ik_log_only = false;
    config.arm_ik_block_animation = true;
    config.motion_attacks = config.physical_attacks = true;
    config.physical_defence = true;
    config.dominant_hand_left = false;
    config.guard_settle_ms = 120;
    config.guard_flick_speed_min = 2.2f;
    config.guard_parry_press_ms = 90;
    config.guard_parry_cooldown_ms = 700;
    applied_hands_valid = true;
    applied_hands_epoch = 1;
    long long time = 4000000000LL;
    std::uint64_t epoch = 1;
    int presses = 0, taps = 0, holds = 0;
    bool lt = false;
    long long lt_start = 0;
    const auto shield_side = [] { return config.dominant_hand_left ? 1 : 0; };
    const auto reset = [&] {
        InputInvalidateFrame();
        presses = taps = holds = 0;
        lt = false;
        std::memset(grip_position, 0, sizeof(grip_position));
        // Both hands at guard height: which one is the shield is the config's
        // business, and a hand at the side is tested separately.
        grip_position[0][1] = grip_position[1][1] = 1.0f;
    };
    const auto track = [&] {
        VirtualPadState pad{};
        InputLatestPad(pad);
        const bool now_lt = pad.left_trigger != 0 || (pad.buttons & kPadLeftShoulder) != 0;
        if (now_lt && !lt) {
            lt = true;
            lt_start = time;
        } else if (!now_lt && lt) {
            lt = false;
            const long long ms = (time - lt_start) / 1000000LL;
            // A deliberately rolled-back test clock cannot measure the
            // duration of the hold that began on the previous timeline.
            if (ms >= 0) {
                ++presses;
                if (ms < 200) ++taps; else ++holds;
            }
        }
    };
    const auto step = [&](int side, float speed, bool walking) {
        time += 11111111LL;
        grip_position[side][0] += speed / 90.0f;
        float head[3] = {kHead[0], kHead[1], kHead[2]};
        if (walking) {
            grip_position[1 - side][0] = grip_position[side][0];
            head[0] += grip_position[side][0];
        }
        InputPollFrame(reinterpret_cast<XrSpace>(100), head, epoch, time);
        track();
    };
    const auto hold = [&](float speed, int frames, bool walking = false) {
        for (int n = 0; n < frames; ++n) step(shield_side(), speed, walking);
    };
    const auto weapon_motion = [&](float speed, int frames) {
        const int side = config.dominant_hand_left ? 0 : 1;
        for (int n = 0; n < frames; ++n) step(side, speed, false);
    };

    reset(); hold(0, 40);
    Expect(lt && presses == 0,
           "a raised still shield hand holds the native guard button open");
    InputPollFrame(reinterpret_cast<XrSpace>(100), kHead, epoch, time);
    VirtualPadState repeated_guard{};
    InputLatestPad(repeated_guard);
    Expect((repeated_guard.buttons & kPadLeftShoulder) && !repeated_guard.left_trigger,
           "a duplicate tracked sample preserves the held guard without parrying");
    InputInvalidateFrame();
    lt = false;
    hold(0, 1);
    Expect(!lt, "invalidated guard must settle again before any defence input");
    reset(); grip_position[0][1] = 0.2f; hold(0, 40);
    Expect(!lt && presses == 0, "a shield hand down at the side cannot guard");
    reset(); hold(0, 40); hold(5, 5); hold(0, 40);
    Expect(taps == 1 && holds == 1 && lt,
           "a shield flick taps the guard button once, then the guard returns");
    hold(5, 5); hold(0, 40);
    Expect(taps == 1 && holds == 2,
           "a second flick inside the parry cooldown cannot tap again");
    reset(); hold(0, 40); hold(5, 30); hold(0, 40);
    Expect(taps == 0, "a shield hand that keeps moving is a swing, not a parry");
    reset(); hold(0, 40); hold(1.2f, 5); hold(0, 40);
    Expect(taps == 0 && holds == 1 && lt,
           "motion between the still and flick bands drops the guard, never parries");
    reset(); grip_position[0][1] = 0.2f; hold(0, 40); hold(5, 5); hold(0, 40);
    Expect(taps == 0 && presses == 0, "a flick with the shield down is not a parry");
    // Keep the translated head at its new position when walking stops.
    reset(); hold(0, 40); hold(5, 5, true); hold(0, 40, true);
    Expect(taps == 0 && presses == 0 && lt,
           "roomscale walking with the shield up is neither guard drop nor flick");
    reset(); hold(0, 40);
    Expect(lt, "the guard is held before tracking drops");
    left_pose_valid = false; hold(0, 2);
    Expect(!lt && holds == 1, "shield-hand tracking loss releases the guard");
    left_pose_valid = true; hold(0, 40);
    Expect(lt && holds == 1 && taps == 0, "the guard re-engages when tracking returns");
    for (int transition = 0; transition < 3; ++transition) {
        reset(); hold(0, 40);
        if (transition == 0) epoch = applied_hands_epoch = 2;
        else if (transition == 1) time += 1000000000LL;
        else time -= 1000000000LL;
        hold(0, 1);
        Expect(!lt, "recenter, polling gap and clock rollback release a held guard");
        hold(0, 40);
        Expect(lt && taps == 0, "a discontinuity requires a fresh guard settle");
        epoch = applied_hands_epoch = 1;

        reset(); hold(0, 40); hold(5, 5);
        if (transition == 0) epoch = applied_hands_epoch = 2;
        else if (transition == 1) time += 1000000000LL;
        else time -= 1000000000LL;
        hold(0, 1);
        VirtualPadState after{};
        InputLatestPad(after);
        Expect(after.left_trigger == 0 && !(after.buttons & kPadLeftShoulder),
               "a discontinuity cannot turn an unfinished flick into parry or block");
        hold(0, 40);
        Expect(taps == 0, "discarded flick cannot return as a delayed parry");
        epoch = applied_hands_epoch = 1;
    }
    config.physical_attacks = false;
    reset(); hold(0, 40); hold(5, 5); hold(0, 40);
    Expect(taps == 1 && holds == 1 && lt,
           "defence preview works independently of the attack-preview toggle");
    config.motion_attacks = false;
    reset(); hold(0, 40);
    Expect(lt, "physical defence remains available when motion attacks are disabled");
    config.motion_attacks = true;
    config.physical_attacks = true;
    config.dominant_hand_left = true;
    reset(); hold(0, 40); hold(5, 5); hold(0, 40);
    Expect(taps == 1 && holds == 1 && lt,
           "the left-hand shield guards and parries the same way");
    reset(); hold(0, 40); weapon_motion(5, 5); hold(0, 40);
    Expect(taps == 0 && presses == 0 && lt,
           "the weapon hand cannot tap the shield button");
    config.dominant_hand_left = false;
    for (int gate = 0; gate < 9; ++gate) {
        config.physical_defence = gate != 0;
        config.motion_attacks = true;
        config.arm_ik_log_only = gate == 1;
        config.arm_ik_block_animation = gate != 2;
        applied_hands_valid = gate != 3;
        g_camera_gameplay_ready = gate != 4;
        applied_hands_epoch = gate == 5 ? 2 : 1;
        sanctuary = gate == 6;
        menu_capture = gate == 7;
        calibration_capture = gate == 8;
        reset(); hold(0, 40); hold(5, 5); hold(0, 40);
        Expect(presses == 0 && !lt,
               "disabled, uncommitted, menu or calibration IK cannot guard physically");
        sanctuary = menu_capture = calibration_capture = false;
        config.physical_defence = config.physical_attacks = config.motion_attacks = true;
        config.arm_ik_log_only = false;
        config.arm_ik_block_animation = applied_hands_valid = true;
        g_camera_gameplay_ready = true;
        applied_hands_epoch = 1;
        hold(0, 40);
        Expect(presses == 0 && lt,
               "the guard resumes on its own once the gate is released again");
    }
    config = saved;
    left_pose_valid = saved_left_pose;
    right_pose_valid = saved_right_pose;
    applied_hands_valid = false;
    applied_hands_epoch = 1;
    reset();
    right_stick_value = 1;
}

void BridgeReadinessLog() {
    using namespace dsvr;
    const auto saved = config;
    const bool saved_left_pose = left_pose_valid;
    const bool saved_right_pose = right_pose_valid;
    left_pose_valid = right_pose_valid = true;
    right_stick_value = 0;
    config.arm_ik_enabled = true;
    config.arm_ik_log_only = false;
    config.arm_ik_block_animation = true;
    config.motion_attacks = config.physical_attacks = config.physical_defence = true;
    config.dominant_hand_left = false;
    const auto base = reinterpret_cast<XrSpace>(100);
    const auto latest_bridges = []() -> std::string {
        const auto lines = dsvr::BridgeLogLines();
        return lines.empty() ? std::string{} : lines.back();
    };
    // Same-frame polling with identical XR time: the second config change must
    // produce a new readiness line naming the newly closed gate.
    dsvr::BridgeLogClear();
    applied_hands_valid = false;
    applied_hands_epoch = 1;
    g_camera_gameplay_ready = true;
    sanctuary = menu_capture = calibration_capture = false;
    InputPollFrame(base, kHead, 1, 3000000000LL);
    const std::string uncommitted = latest_bridges();
    Expect(uncommitted.find("attack=no-committed-hands") != std::string::npos &&
           uncommitted.find("defence=no-committed-hands") != std::string::npos,
           "readiness log names the uncommitted-hands gate while IK has no solve");
    // Committed hands with a stale epoch: the line must change to name it.
    applied_hands_valid = true;
    applied_hands_epoch = 2;
    InputPollFrame(base, kHead, 1, 3000000001LL);
    const std::string stale = latest_bridges();
    Expect(stale != uncommitted &&
           stale.find("attack=hand-epoch-stale") != std::string::npos,
           "readiness log changes to the stale-epoch gate on the next poll");
    // Fresh matching epoch: the line must report ready with the same hand count.
    applied_hands_epoch = 1;
    InputPollFrame(base, kHead, 1, 3000000002LL);
    const std::string ready = latest_bridges();
    Expect(ready != stale && ready.find("attack=ready") != std::string::npos &&
           ready.find("defence=ready") != std::string::npos,
           "readiness log reports ready attacks and guards on matching IK epoch");
    // No new line while nothing changes: the log stays quiet per frame.
    const std::size_t lines_before = dsvr::BridgeLogLines().size();
    InputPollFrame(base, kHead, 1, 3000000003LL);
    Expect(dsvr::BridgeLogLines().size() == lines_before,
           "unchanged readiness emits no repeated log line");
    config = saved;
    left_pose_valid = saved_left_pose;
    right_pose_valid = saved_right_pose;
    applied_hands_valid = false;
    applied_hands_epoch = 1;
    sanctuary = false;
    InputInvalidateFrame();
}

void PhysicalAttacks() {
    using namespace dsvr;
    const auto saved = config;
    const bool saved_left_pose = left_pose_valid;
    const bool saved_right_pose = right_pose_valid;
    left_pose_valid = right_pose_valid = true;
    ResetRoomscaleTestControls();
    right_stick_value = 0;
    config.arm_ik_enabled = true;
    config.arm_ik_log_only = false;
    config.arm_ik_block_animation = true;
    config.motion_attacks = config.physical_attacks = true;
    config.dominant_hand_left = false;
    config.swing_attack_speed_min = 2;
    config.swing_attack_heavy_min = 4.5f;
    
    applied_hands_valid = true;
    applied_hands_epoch = 1;
    long long time = 1000000000LL;
    std::uint64_t epoch = 1;
    int light_edges = 0, heavy_edges = 0;
    bool last_light = false, last_heavy = false;
    VirtualPadState last{};
    const auto reset = [&] {
        InputInvalidateFrame();
        light_edges = heavy_edges = 0;
        last_light = last_heavy = false;
        std::memset(grip_position, 0, sizeof(grip_position));
    };
    const auto move = [&](int side, float speed, int frames, bool thrust = false,
                          bool walking = false) {
        for (int n = 0; n < frames; ++n) {
            time += 11111111LL;
            grip_position[side][thrust ? 2 : 0] += speed / 90.0f;
            float head[3] = {kHead[0], kHead[1], kHead[2]};
            if (walking) {
                grip_position[1-side][0] = grip_position[side][0];
                head[0] += grip_position[side][0];
            }
            InputPollFrame(reinterpret_cast<XrSpace>(100), head, epoch, time);
            InputLatestPad(last);
            const bool light = (last.buttons & kPadRightShoulder) != 0;
            const bool heavy = last.right_trigger != 0;
            light_edges += light && !last_light;
            heavy_edges += heavy && !last_heavy;
            last_light = light;
            last_heavy = heavy;
        }
    };
    reset(); move(1, 0, 40); move(1, 3, 100);
    Expect(light_edges == 1 && heavy_edges == 0 && last_light,
           "physical slash holds the native attack while the sword keeps moving");
    move(1, 0, 40);
    Expect(light_edges == 1 && !last_light,
           "native attack releases once sword speed falls into the stop band");
    reset(); move(1, 0, 40); move(1, 3, 10);
    Expect(last_light && light_edges == 1,
           "a qualified slash starts its held native input immediately");
    InputPollFrame(reinterpret_cast<XrSpace>(100), kHead, epoch, time);
    VirtualPadState repeated_attack{};
    InputLatestPad(repeated_attack);
    Expect((repeated_attack.buttons & kPadRightShoulder) && !repeated_attack.right_trigger,
           "a duplicate tracked sample cannot truncate the attack press window");
    move(1, 3, 30);
    Expect(light_edges == 1 && last_light,
           "duplicate sampling cannot restart or truncate the active attack");
    move(1, 0, 40);
    Expect(!last_light, "low-speed follow-through ends the attack hold");
    reset(); move(1, 0, 40); move(1, 9, 25);
    Expect(heavy_edges == 1 && light_edges == 0,
           "fast physical slash reaches the native strong-attack trigger");
    reset(); move(1, 0, 40); move(1, 3, 30, false, true);
    Expect(light_edges == 0 && heavy_edges == 0,
           "roomscale head and hands translating together cannot attack");
    config.dominant_hand_left = true;
    reset(); move(0, 0, 40); move(1, 6, 30);
    Expect(light_edges == 0 && heavy_edges == 0, "off-hand motion cannot attack");
    move(0, 9, 30);
    Expect(heavy_edges == 1 && light_edges == 0, "left weapon hand can attack");
    config.dominant_hand_left = false;
    for (int gate = 0; gate < 7; ++gate) {
        config.physical_attacks = gate != 0;
        config.motion_attacks = gate != 1;
        config.arm_ik_log_only = gate == 2;
        config.arm_ik_block_animation = gate != 3;
        applied_hands_valid = gate != 4;
        g_camera_gameplay_ready = gate != 5;
        applied_hands_epoch = gate == 6 ? 2 : 1;
        reset(); move(1, 0, 40); move(1, 6, 30);
        Expect(light_edges == 0 && heavy_edges == 0,
               "disabled, uncommitted, menu or wrong-epoch IK cannot attack");
    }
    config.physical_attacks = config.motion_attacks = true;
    config.arm_ik_log_only = false;
    config.arm_ik_block_animation = applied_hands_valid = true;
    g_camera_gameplay_ready = true;
    applied_hands_epoch = 1;
    reset(); move(1, 0, 40);
    right_pose_valid = false; move(1, 0, 1); right_pose_valid = true;
    move(1, 3, 30);
    Expect(light_edges == 0, "tracking loss discards the armed physical swing");
    reset(); move(1, 0, 40); InputInvalidateFrame(); move(1, 3, 30);
    Expect(light_edges == 0, "input invalidation requires a fresh physical rest");
    reset(); move(1, 0, 40); epoch = applied_hands_epoch = 2; move(1, 3, 30);
    Expect(light_edges == 0, "recenter discards armed swing history");
    epoch = applied_hands_epoch = 1;
    config.thrust_enabled = false;
    reset(); move(1, 0, 40); move(1, 1.7f, 30, true);
    Expect(light_edges == 0, "disabled thrust cannot lower the attack threshold");
    config.thrust_enabled = true;
    reset(); move(1, 0, 40); move(1, 1.7f, 30, true);
    Expect(light_edges == 1, "enabled thrust uses its lower physical speed bar");
    config.swing_attack_speed_min = 0.5f;
    config.swing_attack_heavy_min = 1.0f;
    reset(); move(1, 0, 40); move(1, 0.7f, 30);
    Expect(light_edges == 1 && heavy_edges == 0,
           "lowest configured light threshold remains usable");
    config.swing_attack_speed_min = 2.0f;
    config.swing_attack_heavy_min = 1.0f;
    reset(); move(1, 0, 40); move(1, 3, 30);
    Expect(heavy_edges == 1, "crossed speed sliders cannot disable physical input");
    config.swing_attack_heavy_min = 4.5f;
    for (int gate = 0; gate < 5; ++gate) {
        reset(); move(1, 0, 40);
        sanctuary = gate == 0;
        menu_capture = gate == 1;
        calibration_capture = gate == 2;
        config.physical_attacks = gate != 3;
        config.arm_ik_log_only = gate == 4;
        move(1, 6, 30);
        Expect(light_edges == 0 && heavy_edges == 0,
               "menu, calibration and disabled modes cannot emit physical attacks");
        sanctuary = menu_capture = calibration_capture = false;
        config.physical_attacks = true;
        config.arm_ik_log_only = false;
        move(1, 6, 30);
        Expect(light_edges == 0 && heavy_edges == 0,
               "returning to gameplay cannot replay a disabled swing");
    }
    reset(); move(1, 0, 40); time += 1000000000LL; move(1, 3, 30);
    Expect(light_edges == 0 && heavy_edges == 0,
           "a polling gap discards the armed physical swing");
    config = saved;
    left_pose_valid = saved_left_pose;
    right_pose_valid = saved_right_pose;
    applied_hands_valid = false;
    applied_hands_epoch = 1;
    reset();
    right_stick_value = 1;
}

// A rested reach with no bound native target must not tap interact.
// InteractionTargetEligibility has no producer yet (H1 prompt/LOS owner
// open), so the default refuses everything and the reach path logs the
// refusal instead of silently pressing A at a wall or an enemy's face.
void UntargetedReachRefuses() {
    using namespace dsvr;
    const auto saved = config;
    const bool saved_left_pose = left_pose_valid;
    const bool saved_right_pose = right_pose_valid;
    left_pose_valid = right_pose_valid = true;
    ResetRoomscaleTestControls();
    config.reach_interact = true;
    config.arm_ik_enabled = true;
    config.arm_ik_log_only = false;
    config.arm_ik_block_animation = true;
    config.motion_attacks = true;
    applied_hands_valid = true;
    applied_hands_epoch = 1;
    InputInvalidateFrame();
    const auto base = reinterpret_cast<XrSpace>(100);
    long long time = 6000000000LL;
    const std::uint64_t epoch = 1;
    VirtualPadState pad{};
    // Both hands rest, then the left hand thrusts forward: the exact shape
    // the reach bridge arms on. Head-relative forward is -Z of the base
    // space, so advancing +X with the head looking +X-equivalent is a
    // sideways sweep, not a reach; the fixture instead moves the hand along
    // the head-forward axis by editing grip Z against a fixed head.
    const auto poll = [&] {
        InputPublishHeadDirectedLocomotion({0, 0, 1}, {0, 0, 1}, 1);
        InputPollFrame(base, kHead, epoch, time);
        time += 11111111LL;
        Expect(InputLatestPad(pad), "valid actions keep the pad available");
    };
    for (int n = 0; n < 40; ++n) {
        grip_position[0][0] = grip_position[0][1] = grip_position[0][2] = 0.0f;
        grip_position[1][0] = grip_position[1][1] = grip_position[1][2] = 0.0f;
        poll();
    }
    bool any_a = false;
    for (int n = 0; n < 30; ++n) {
        // ~2.4 m/s forward: head at z=0, grip z advances 0.027 m/frame.
        grip_position[0][2] -= 0.027f;
        poll();
        any_a = any_a || ((pad.buttons & kPadA) != 0);
    }
    Expect(!any_a, "an untargeted reach never taps native interact");
    config = saved;
    left_pose_valid = saved_left_pose;
    right_pose_valid = saved_right_pose;
    applied_hands_valid = false;
    InputInvalidateFrame();
}

// A stale published selection must not arm a later reach: the store's
// freshness window binds the gesture to a live native row, not a reused
// handle from an earlier callback.
void StaleSelectionNeverTaps() {
    using namespace dsvr;
    const auto saved = config;
    const bool saved_left_pose = left_pose_valid;
    const bool saved_right_pose = right_pose_valid;
    left_pose_valid = right_pose_valid = true;
    ResetRoomscaleTestControls();
    config.reach_interact = true;
    config.arm_ik_enabled = true;
    config.arm_ik_log_only = false;
    config.arm_ik_block_animation = true;
    config.motion_attacks = true;
    applied_hands_valid = true;
    applied_hands_epoch = 1;
    InputInvalidateFrame();
    const auto base = reinterpret_cast<XrSpace>(100);
    long long time = 6000000000LL;
    const std::uint64_t epoch = 1;
    VirtualPadState pad{};
    const auto poll = [&] {
        InputPublishHeadDirectedLocomotion({0, 0, 1}, {0, 0, 1}, 1);
        InputPollFrame(base, kHead, epoch, time);
        time += 11111111LL;
        Expect(InputLatestPad(pad), "valid actions keep the pad available");
    };
    for (int n = 0; n < 40; ++n) {
        grip_position[0][0] = grip_position[0][1] = grip_position[0][2] = 0.0f;
        grip_position[1][0] = grip_position[1][1] = grip_position[1][2] = 0.0f;
        poll();
    }
    // Publish a bound selection, then let it go stale before thrusting:
    // the gate must refuse even though a selection was once bound.
    InteractionSelection stale{};
    stale.row_handle = 111u;
    stale.object_handle = 0x21d4c9beu;
    stale.generation = 1u;
    stale.dummy_valid = true;
    stale.dummy_pos[0] = 0.5f;
    InputPublishInteractionSelection(stale, time / 1000000LL - 1000LL);
    bool any_a = false;
    for (int n = 0; n < 30; ++n) {
        grip_position[0][2] -= 0.027f;
        poll();
        any_a = any_a || ((pad.buttons & kPadA) != 0);
    }
    Expect(!any_a, "a stale selection never taps native interact");
    InputInvalidateInteractionSelection();
    config = saved;
    left_pose_valid = saved_left_pose;
    right_pose_valid = saved_right_pose;
    applied_hands_valid = false;
    InputInvalidateFrame();
}

// A fresh bound selection still refuses the reach tap: the eligibility
// producer (prompt/LOS/lifecycle) does not exist yet, so the gesture +
// selection pair must not tap A until that producer binds. This pins the
// current fail-closed posture; when the producer lands, this test flips
// to expect the tap.
void FreshSelectionStillRefusesWithoutEligibility() {
    using namespace dsvr;
    const auto saved = config;
    const bool saved_left_pose = left_pose_valid;
    const bool saved_right_pose = right_pose_valid;
    left_pose_valid = right_pose_valid = true;
    ResetRoomscaleTestControls();
    config.reach_interact = true;
    config.arm_ik_enabled = true;
    config.arm_ik_log_only = false;
    config.arm_ik_block_animation = true;
    config.motion_attacks = true;
    applied_hands_valid = true;
    applied_hands_epoch = 1;
    InputInvalidateFrame();
    const auto base = reinterpret_cast<XrSpace>(100);
    long long time = 6000000000LL;
    const std::uint64_t epoch = 1;
    VirtualPadState pad{};
    const auto poll = [&] {
        InputPublishHeadDirectedLocomotion({0, 0, 1}, {0, 0, 1}, 1);
        InputPollFrame(base, kHead, epoch, time);
        time += 11111111LL;
        Expect(InputLatestPad(pad), "valid actions keep the pad available");
    };
    for (int n = 0; n < 40; ++n) {
        grip_position[0][0] = grip_position[0][1] = grip_position[0][2] = 0.0f;
        grip_position[1][0] = grip_position[1][1] = grip_position[1][2] = 0.0f;
        poll();
    }
    // Publish a fresh bound selection on every frame, then thrust: the
    // store is live but eligibility has no producer, so the gate refuses.
    bool any_a = false;
    for (int n = 0; n < 30; ++n) {
        grip_position[0][2] -= 0.027f;
        InteractionSelection fresh{};
        fresh.row_handle = 111u;
        fresh.object_handle = 0x21d4c9beu;
        fresh.generation = 1u;
        fresh.dummy_valid = true;
        fresh.dummy_pos[0] = 0.5f;
        InputPublishInteractionSelection(fresh, time / 1000000LL);
        poll();
        any_a = any_a || ((pad.buttons & kPadA) != 0);
    }
    Expect(!any_a, "a fresh selection without eligibility never taps native interact");
    InputInvalidateInteractionSelection();
    config = saved;
    left_pose_valid = saved_left_pose;
    right_pose_valid = saved_right_pose;
    applied_hands_valid = false;
    InputInvalidateFrame();
}

// A weapon swap, stance change or new player actor must not leave an armed
// swing from the previous equipment alive on the pad, and must not kill the
// bridge for the new equipment either.
void EquipmentReprime() {
    using namespace dsvr;
    const auto saved = config;
    const auto saved_status = g_combat_status;
    const bool saved_left_pose = left_pose_valid;
    const bool saved_right_pose = right_pose_valid;
    left_pose_valid = right_pose_valid = true;
    ResetRoomscaleTestControls();
    right_stick_value = 0;
    config.arm_ik_enabled = true;
    config.arm_ik_log_only = false;
    config.arm_ik_block_animation = true;
    config.motion_attacks = config.physical_attacks = true;
    config.dominant_hand_left = false;
    config.swing_attack_speed_min = 2;
    config.swing_attack_heavy_min = 4.5f;
    
    applied_hands_valid = true;
    applied_hands_epoch = 9;
    g_combat_status = {};
    g_combat_status.equipment_bound = true;
    g_combat_status.equipment.player = 0x1000;
    g_combat_status.equipment.weapon_id[1] = 7;
    long long time = 4000000000LL;
    const std::uint64_t epoch = 9;
    int light_edges = 0, heavy_edges = 0;
    bool last_light = false, last_heavy = false;
    VirtualPadState last{};
    const auto reset = [&] {
        InputInvalidateFrame();
        light_edges = heavy_edges = 0;
        last_light = last_heavy = false;
        std::memset(grip_position, 0, sizeof(grip_position));
    };
    const auto move = [&](int side, float speed, int frames) {
        for (int n = 0; n < frames; ++n) {
            time += 11111111LL;
            grip_position[side][0] += speed / 90.0f;
            InputPollFrame(reinterpret_cast<XrSpace>(100), kHead, epoch, time);
            InputLatestPad(last);
            const bool light = (last.buttons & kPadRightShoulder) != 0;
            const bool heavy = last.right_trigger != 0;
            light_edges += light && !last_light;
            heavy_edges += heavy && !last_heavy;
            last_light = light;
            last_heavy = heavy;
        }
    };
    reset(); move(1, 0, 40); move(1, 9, 4);
    g_combat_status.equipment.weapon_id[1] = 42;
    move(1, 9, 30);
    Expect(light_edges == 0 && heavy_edges == 1,
           "a weapon swap does not replay the already-started physical swing");
    reset(); move(1, 0, 40); move(1, 9, 30);
    Expect(heavy_edges == 1 && light_edges == 0,
           "the new weapon starts immediately after a new motion edge");
    reset(); move(1, 0, 40); move(1, 9, 4);
    g_combat_status.equipment.native_stance = 1;
    move(1, 9, 30);
    Expect(light_edges == 0 && heavy_edges == 1,
           "a stance change does not replay the active swing");
    reset(); move(1, 0, 40); move(1, 9, 4);
    g_combat_status.equipment.player = 0x2000;
    move(1, 9, 30);
    Expect(light_edges == 0 && heavy_edges == 1,
           "a new player actor does not replay the active swing");
    reset(); move(1, 0, 40); move(1, 9, 30);
    Expect(heavy_edges == 1, "the bridge remains live after identity reprime");
    config = saved;
    g_combat_status = saved_status;
    left_pose_valid = saved_left_pose;
    right_pose_valid = saved_right_pose;
    applied_hands_valid = false;
    applied_hands_epoch = 1;
    reset();
    right_stick_value = 1;
}
void CapturedInput() {
    const XrSpace base = reinterpret_cast<XrSpace>(100);
    config.arm_ik_enabled = false;
    dsvr::InputInvalidateFrame();
    dsvr::InputPublishHeadDirectedLocomotion({0, 0, 1}, {0, 0, 1}, 1);
    calibration_capture = true;
    left_stick_x = -0.6f;
    left_stick_y = 0.8f;
    right_secondary = true;
    right_trigger = 1;
    dsvr::InputPollFrame(base, kHead, 1, 2000000000);
    dsvr::VirtualPadState pad{};
    Expect(dsvr::InputLatestPad(pad) && pad.right_trigger == 0 &&
           pad.buttons == 0 && pad.thumb_lx == 0 && pad.thumb_ly == 0,
           "calibration captures gameplay buttons and locomotion");
    dsvr::HandSample hand{};
    Expect(dsvr::InputLatestHand(false, hand) && hand.valid && hand.trigger_held,
           "captured calibration keeps publishing tracked hands and trigger");
    Expect(dsvr::InputTakeTurnYaw() == 0, "calibration discards queued turns");
    Expect(dsvr::InputLatestComfortMotion().move == 0 &&
           dsvr::InputLatestComfortMotion().turn_radians == 0,
           "captured calibration controls cannot activate the comfort vignette");
    Expect(menu_x == -0.6f && menu_y == 0.8f &&
           !menu_selected && menu_back,
           "calibration receives left-stick navigation and B/back without selecting on the hold trigger");
    right_primary = true;
    right_secondary = false;
    dsvr::InputPollFrame(base, kHead, 1, 2005000000);
    Expect(menu_x == -0.6f && menu_y == 0.8f &&
           menu_selected && !menu_back,
           "A selects calibration controls while B/back releases and the hold trigger stays reserved");
    Expect(dsvr::InputLatestPad(pad) && pad.buttons == 0 && pad.right_trigger == 0,
           "calibration A selection and the held trigger stay captured from gameplay");
    right_primary = false;
    calibration_capture = false;
    dsvr::InputPollFrame(base, kHead, 1, 2010000000);
    Expect(dsvr::InputLatestPad(pad) && pad.right_trigger == 0,
           "the calibration completion trigger cannot leak into gameplay");
    right_trigger = 0;
    right_stick_value = 0;
    left_stick_x = 0;
    left_stick_y = 0;
    dsvr::InputPollFrame(base, kHead, 1, 2020000000);
    right_trigger = 1;
    dsvr::InputPollFrame(base, kHead, 1, 2030000000);
    Expect(dsvr::InputLatestPad(pad) && pad.right_trigger == 255,
           "gameplay resumes after controls are released and pressed again");
    menu_capture = true;
    left_stick_x = 0.45f;
    left_stick_y = -0.9f;
    dsvr::InputPollFrame(base, kHead, 1, 2040000000);
    Expect(menu_x == 0.45f && menu_y == -0.9f &&
           !menu_selected && !menu_back,
           "Home receives navigation without duplicating the ray trigger as focus confirm");
    Expect(dsvr::InputLatestPad(pad) && pad.right_trigger == 0 &&
           pad.buttons == 0 && pad.thumb_lx == 0 && pad.thumb_ly == 0,
           "Home trigger selection and navigation stay captured from gameplay");
    right_trigger = 0;
    right_primary = true;
    dsvr::InputPollFrame(base, kHead, 1, 2045000000);
    Expect(menu_x == 0.45f && menu_y == -0.9f &&
           menu_selected && !menu_back,
           "A alone selects Home controls without asserting B/back");
    Expect(dsvr::InputLatestPad(pad) && pad.buttons == 0 && pad.right_trigger == 0,
           "Home A selection does not leak a native confirm or attack");
    right_primary = false;
    right_secondary = true;
    dsvr::InputPollFrame(base, kHead, 1, 2046000000);
    Expect(menu_x == 0.45f && menu_y == -0.9f &&
           !menu_selected && menu_back,
           "right-hand B alone requests Home back without selecting");
    Expect(dsvr::InputLatestPad(pad) && pad.buttons == 0 && pad.right_trigger == 0,
           "Home B/back does not leak a native cancel or attack");
    right_secondary = false;
    right_trigger = 1;
    dsvr::InputPollFrame(base, kHead, 1, 2047000000);
    Expect(menu_x == 0.45f && menu_y == -0.9f &&
           !menu_selected && !menu_back,
           "Home trigger stays on the pointer route after B/back releases");
    menu_capture = false;
    dsvr::InputPollFrame(base, kHead, 1, 2050000000);
    Expect(dsvr::InputLatestPad(pad) && pad.right_trigger == 0 &&
           pad.buttons == 0 && pad.thumb_lx == 0 && pad.thumb_ly == 0,
           "closing Home with trigger and navigation held cannot leak into gameplay");
    right_trigger = 0;
    dsvr::InputPollFrame(base, kHead, 1, 2051000000);
    Expect(dsvr::InputLatestPad(pad) && pad.right_trigger == 0 &&
           pad.buttons == 0 && pad.thumb_lx == 0 && pad.thumb_ly == 0,
           "releasing only the trigger does not re-arm gameplay while the stick is held");
    right_primary = true;
    dsvr::InputPollFrame(base, kHead, 1, 2051500000);
    Expect(dsvr::InputLatestPad(pad) && (pad.buttons & dsvr::kPadA) &&
           pad.thumb_lx == 0 && pad.thumb_ly == 0 && pad.right_trigger == 0,
           "a newly pressed A works while the captured navigation stick still awaits release");
    right_primary = false;
    left_stick_x = 0;
    left_stick_y = 0;
    dsvr::InputPollFrame(base, kHead, 1, 2052000000);
    Expect(dsvr::InputLatestPad(pad) && pad.right_trigger == 0 &&
           pad.buttons == 0 && pad.thumb_lx == 0 && pad.thumb_ly == 0,
           "the neutral release frame after closing Home remains neutral");
    right_trigger = 1;
    left_stick_x = 0.45f;
    left_stick_y = -0.9f;
    dsvr::InputPollFrame(base, kHead, 1, 2053000000);
    Expect(dsvr::InputLatestPad(pad) && pad.right_trigger == 255 &&
           pad.thumb_lx != 0 && pad.thumb_ly != 0,
           "gameplay attack and locomotion re-arm only after a neutral release and new input");
    right_trigger = 0;
    left_stick_x = 0;
    left_stick_y = 0;

    menu_capture = true;
    right_squeeze = 1;
    dsvr::InputPollFrame(base, kHead, 1, 2054000000);
    menu_capture = false;
    right_primary = true;
    left_stick_y = 0.8f;
    dsvr::InputPollFrame(base, kHead, 1, 2055000000);
    Expect(dsvr::InputLatestPad(pad) && (pad.buttons & dsvr::kPadA) &&
           !(pad.buttons & dsvr::kPadRightShoulder) && pad.thumb_ly != 0,
           "holding an old grip cannot globally mute fresh confirm and walking after Home");
    right_primary = false;
    left_stick_y = 0;
    right_squeeze = 0;
    dsvr::InputPollFrame(base, kHead, 1, 2056000000);
    for (int mode = 0; mode < 2; ++mode) {
      const long long time = 2057000000LL + mode * 1000000LL;
      sanctuary = mode == 0;
      dsvr::g_camera_gameplay_ready = mode == 0;
      calibration_capture = mode == 1;
      menu_capture = mode == 0;
      right_squeeze = 1;
      dsvr::InputPollFrame(base, kHead, 1, time);
      menu_capture = calibration_capture = false;
      right_primary = true;
      left_stick_y = 0.9f;
      dsvr::InputPollFrame(base, kHead, 1, time + 100000);
      Expect(dsvr::InputLatestPad(pad) &&
             (pad.buttons & (dsvr::kPadA | dsvr::kPadDpadUp)) ==
                 (dsvr::kPadA | dsvr::kPadDpadUp) &&
             !(pad.buttons & dsvr::kPadRightShoulder) && pad.thumb_ly == 0,
             "theater and front-end confirm/navigation survive an unrelated captured grip");
      right_primary = false;
      left_stick_y = right_squeeze = 0;
      dsvr::InputPollFrame(base, kHead, 1, time + 200000);
    }
    sanctuary = false;
    dsvr::g_camera_gameplay_ready = true;
    menu_held = true;
    right_squeeze = 1;
    dsvr::InputPollFrame(base, kHead, 1, 2060000000);
    Expect(dsvr::InputLatestPad(pad) && pad.buttons == 0,
           "pending menu chord reserves native Start and attack grip");
    dsvr::InputPollFrame(base, kHead, 1, 2570000000);
    Expect(menu_toggles == 1 && menu_capture,
           "holding Menu plus right grip opens the mod menu");
    dsvr::InputPollFrame(base, kHead, 1, 3100000000);
    Expect(menu_toggles == 1, "a held menu chord toggles exactly once");
    menu_held = false;
    right_squeeze = 0;
    dsvr::InputPollFrame(base, kHead, 1, 3110000000);
}
} // namespace

namespace dsvr {
Config DiagnosticsCurrentConfig() { return config; }
bool VrTraceActive() { return trace_active; }
bool VrTraceSessionActive() { return trace_active; }
std::uint64_t VrTraceCaptureId() { return trace_capture_id; }
void VrTraceRecord(VrTraceEvent event) { trace_events.push_back(event); }
CombatNativeStatus CombatNativeGetStatus() { return g_combat_status; }
bool ArmIkLatestHands(ArmIkHands& out) {
    out = {};
    out.sequence = 1;
    out.epoch = applied_hands_epoch;
    out.side[0].valid = out.side[1].valid = applied_hands_valid;
    return applied_hands_valid;
}
void ArmIkDrainBenchHaptics() {}  // iteration 2: no bench in input tests
bool DiagnosticsReloadConfig() { return true; }
void DiagnosticsLogMarker() {}
void XrToggleSanctuary() { ++theater_toggles; }
void XrRecenterSanctuary() { ++recenter_calls; }
bool XrMenuPresentationActive() { return sanctuary; }
bool CalibrationCapturesInput() { return calibration_capture; }
void CalibrationRequest() { ++calibration_requests; calibration_capture = true; }
bool ModMenuCapturesInput() { return menu_capture; }
void ModMenuToggle() { ++menu_toggles; menu_capture = !menu_capture; }
void ModMenuControllerInput(float x, float y, bool select, bool back,
                             bool prev_panel, bool next_panel) {
    menu_x = x;
    menu_y = y;
    menu_selected = select;
    menu_back = back;
    menu_prev = prev_panel;
    menu_next = next_panel;
}
void LogWrite(LogLevel, const char*, int, const char* fmt, ...) {
    if (!fmt || std::strstr(fmt, "InputBridges:") == nullptr) return;
    char message[1024]{};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    bridge_log_lines.push_back(message);
}
std::vector<std::string> BridgeLogLines() { return bridge_log_lines; }
void BridgeLogClear() { bridge_log_lines.clear(); }
} // namespace dsvr

extern "C" {
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSet(XrInstance, const XrActionSetCreateInfo*, XrActionSet* out) {
    *out = Handle<XrActionSet>();
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrStringToPath(XrInstance, const char* text, XrPath* out) {
    auto [entry, inserted] = paths.try_emplace(text, XR_NULL_PATH);
    if (inserted) entry->second = static_cast<XrPath>(next_handle++);
    *out = entry->second;
    path_names[*out] = text;
    if (std::strcmp(text, "/user/hand/left") == 0) left_path = *out;
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateAction(XrActionSet, const XrActionCreateInfo* info, XrAction* out) {
    *out = Handle<XrAction>();
    if (std::strcmp(info->actionName, "thumbstick") == 0) stick_action = *out;
    if (std::strcmp(info->actionName, "aim_pose") == 0) aim_action = *out;
    if (std::strcmp(info->actionName, "squeeze") == 0) squeeze_action = *out;
    if (std::strcmp(info->actionName, "trigger") == 0) trigger_action = *out;
    if (std::strcmp(info->actionName, "menu") == 0) menu_action = *out;
    if (std::strcmp(info->actionName, "primary") == 0) primary_action = *out;
    if (std::strcmp(info->actionName, "secondary") == 0) secondary_action = *out;
    if (std::strcmp(info->actionName, "stick_click") == 0) stick_click_action = *out;
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(XrInstance, const XrInteractionProfileSuggestedBinding* info) {
    const auto& profile = path_names[info->interactionProfile];
    if (profile == "/interaction_profiles/oculus/touch_controller") {
        for (uint32_t i = 0; i < info->countSuggestedBindings; ++i) {
            const auto& path = path_names[info->suggestedBindings[i].binding];
            const bool left = path.starts_with("/user/hand/left/");
            const bool right = path.starts_with("/user/hand/right/");
            const std::string component = path.substr(left ? 15 : 16);
            bool supported = false;
            for (const char* common : {"/input/grip/pose", "/input/aim/pose",
                    "/input/squeeze/value", "/input/trigger/value",
                    "/input/thumbstick", "/input/thumbstick/click", "/output/haptic"})
                supported |= component == common;
            supported |= left && (component == "/input/x/click" ||
                component == "/input/y/click" || component == "/input/menu/click");
            supported |= right && (component == "/input/a/click" ||
                component == "/input/b/click");
            if ((!left && !right) || !supported) return XR_ERROR_PATH_UNSUPPORTED;
        }
    }
    suggested_bindings[profile].assign(info->suggestedBindings,
                                     info->suggestedBindings + info->countSuggestedBindings);
    ++suggestion_counts[profile];
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrAttachSessionActionSets(XrSession session, const XrSessionActionSetsAttachInfo*) {
    static std::map<XrSession, bool> attached;
    if (attached[session]) return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
    attached[session] = true;
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSpace(XrSession, const XrActionSpaceCreateInfo* info, XrSpace* out) {
    *out = reinterpret_cast<XrSpace>(static_cast<std::uintptr_t>(
        (info->action==aim_action?210:200)+(info->subactionPath==left_path?0:1)));
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL stub_destroy_action_space(XrSpace) { ++destroyed_spaces; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL stub_destroy_action_set(XrActionSet) { ++destroyed_sets; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance, const char* name, PFN_xrVoidFunction* out) {
    if (std::strcmp(name, "xrDestroySpace") == 0) {
        *out = reinterpret_cast<PFN_xrVoidFunction>(&stub_destroy_action_space);
        return XR_SUCCESS;
    }
    if (std::strcmp(name, "xrDestroyActionSet") == 0) {
        *out = reinterpret_cast<PFN_xrVoidFunction>(&stub_destroy_action_set);
        return XR_SUCCESS;
    }
    *out = nullptr;
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateReferenceSpace(XrSession, const XrReferenceSpaceCreateInfo*, XrSpace* out) {
    view_space = Handle<XrSpace>();
    *out = view_space;
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrSyncActions(XrSession, const XrActionsSyncInfo*) {
    return sync_result;
}
XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace space, XrSpace, XrTime, XrSpaceLocation* out) {
    out->locationFlags = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    const bool is_aim=space==reinterpret_cast<XrSpace>(210) || space==reinterpret_cast<XrSpace>(211);
    if(is_aim && !aim_pose_valid) out->locationFlags=0;
    const float* q = is_aim ? aim_quat : space == view_space ? head_quat :
        (space == reinterpret_cast<XrSpace>(static_cast<std::uintptr_t>(200)) ? left_quat : right_quat);
    if (space == reinterpret_cast<XrSpace>(static_cast<std::uintptr_t>(200)) && !left_pose_valid)
        out->locationFlags &= ~XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if (space == reinterpret_cast<XrSpace>(static_cast<std::uintptr_t>(201)) && !right_pose_valid)
        out->locationFlags &= ~XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    out->pose.orientation = {q[0], q[1], q[2], q[3]};
    if (out->next) {
      auto* velocity = reinterpret_cast<XrSpaceVelocity*>(out->next);
      velocity->velocityFlags = XR_SPACE_VELOCITY_LINEAR_VALID_BIT |
                                XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
      velocity->linearVelocity = {1.0f, 2.0f, 3.0f};
      velocity->angularVelocity = {.1f, .2f, .3f};
    }
    const int side = space == reinterpret_cast<XrSpace>(200) ? 0 : 1;
    if (!is_aim && space != view_space)
      out->pose.position = {grip_position[side][0], grip_position[side][1], grip_position[side][2]};
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStatePose(XrSession, const XrActionStateGetInfo*, XrActionStatePose* out) {
    out->isActive=aim_active?XR_TRUE:XR_FALSE;return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateBoolean(XrSession, const XrActionStateGetInfo* info, XrActionStateBoolean* out) {
    out->isActive = XR_TRUE;
    out->currentState = info->action == menu_action && info->subactionPath == left_path && menu_held ? XR_TRUE : XR_FALSE;
    if (info->action == primary_action && info->subactionPath != left_path)
        out->currentState = right_primary ? XR_TRUE : XR_FALSE;
    if (info->action == secondary_action && info->subactionPath != left_path)
        out->currentState = right_secondary ? XR_TRUE : XR_FALSE;
    if (info->action == secondary_action && info->subactionPath == left_path)
        out->currentState = left_secondary ? XR_TRUE : XR_FALSE;
     if (info->action == primary_action && info->subactionPath == left_path)
        out->currentState = left_primary ? XR_TRUE : XR_FALSE;
    if (info->action == stick_click_action)
        out->currentState = (info->subactionPath == left_path ? left_stick_click : right_stick_click) ? XR_TRUE : XR_FALSE;
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateFloat(XrSession, const XrActionStateGetInfo* info, XrActionStateFloat* out) {
    out->isActive = XR_TRUE;
    out->currentState = info->subactionPath != left_path ?
        (info->action == trigger_action ? right_trigger :
         (info->action == squeeze_action ? right_squeeze : 0.0f)) : 0.0f;
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateVector2f(XrSession, const XrActionStateGetInfo* info, XrActionStateVector2f* out) {
    const bool right_stick = info->action == stick_action && info->subactionPath != left_path;
    if (right_stick && ++right_stick_queries == fail_right_query)
        return XR_ERROR_RUNTIME_FAILURE;
    if (right_stick && XR_FAILED(turn_result)) return turn_result;
    out->isActive = XR_TRUE;
    out->currentState = {right_stick ? right_stick_value : left_stick_x,
                         right_stick ? 0.0f : left_stick_y};
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrApplyHapticFeedback(XrSession, const XrHapticActionInfo*, const XrHapticBaseHeader*) {
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrResultToString(XrInstance, XrResult result, char out[XR_MAX_RESULT_STRING_SIZE]) {
    std::snprintf(out, XR_MAX_RESULT_STRING_SIZE, "result=%d", static_cast<int>(result));
    return XR_SUCCESS;
}
} // extern "C"

int main() {
    config.xr_input_enabled = true;
    dsvr::InputAttachSession(reinterpret_cast<XrInstance>(1), reinterpret_cast<XrSession>(2));
    Expect(dsvr::InputAvailable(), "fake runtime attaches the actual input implementation");
    ProfileBindings();
    {
      const auto saved = config;
      config.vr_enabled = true;
      config.xr_input_enabled = true;
      config.motion_attacks = false;
      config.arm_ik_enabled = false;
      left_stick_x = 0.25f;
      left_stick_y = 0;
      right_pose_valid = left_pose_valid = true;
      trace_events.clear();
      trace_active = true;
      trace_capture_id = 700;
      const auto base = reinterpret_cast<XrSpace>(100);
      dsvr::InputPollFrame(base, kHead, 5, 1000000000LL);
      dsvr::InputPollFrame(base, kHead, 5, 1010000000LL);
      auto count_event = [](const char* name) {
        return std::count_if(trace_events.begin(), trace_events.end(),
            [name](const auto& event) { return std::strcmp(event.event, name) == 0; });
      };
      Expect(count_event("virtual_pad_state") == 1,
             "session capture emits one initial effective pad state");
      Expect(count_event("hand_tracking") == 2,
             "session capture emits one initial hand-validity baseline per hand");
      left_stick_x = 0.75f;
      dsvr::InputPollFrame(base, kHead, 5, 1020000000LL);
      Expect(count_event("virtual_pad_state") == 2,
             "effective virtual-pad changes emit exactly once");
      dsvr::InputPollFrame(base, kHead, 5, 1030000000LL);
      Expect(count_event("hand_tracking") == 2,
             "steady hand samples stay below the 30 Hz cap");
      dsvr::InputPollFrame(base, kHead, 5, 1033333340LL);
      Expect(count_event("hand_tracking") == 4,
             "valid tracked hands sample at the bounded cadence");
      right_pose_valid = false;
      right_trigger = 1;
      dsvr::InputPollFrame(base, kHead, 5, 1035000000LL);
      Expect(count_event("hand_tracking") == 5,
             "tracking-loss transition is recorded immediately");
      const auto loss = std::find_if(trace_events.rbegin(), trace_events.rend(),
          [](const auto& event) {
            return std::strcmp(event.event, "hand_tracking") == 0 &&
                   event.values[0] == 1.0f;
          });
      Expect(loss != trace_events.rend() && (loss->flags & 3u) == 2u &&
             loss->epoch == 5 && loss->predicted_ns == 1035000000LL &&
             loss->values[14] == 2.0f,
             "hand event preserves loss, held input, space epoch, and XR timestamp");
      const auto left = std::find_if(trace_events.rbegin(), trace_events.rend(),
          [](const auto& event) {
            return std::strcmp(event.event, "hand_tracking") == 0 &&
                   event.values[0] == 0.0f && (event.flags & 1u);
          });
      Expect(left != trace_events.rend() && (left->flags & 12u) == 12u &&
             left->values[8] == 1.0f && left->values[11] == .1f, "hand sample preserves validated linear and angular velocities");
          menu_capture = true;
          dsvr::InputPollFrame(base, kHead, 5, 1040000000LL);
          auto last_pad = [&]() {
            return std::find_if(trace_events.rbegin(), trace_events.rend(), [](const auto& event) {
              return std::strcmp(event.event, "virtual_pad_state") == 0;
            });
          };
          auto cleared = last_pad();
          Expect(cleared != trace_events.rend() && cleared->epoch == 5 &&
                 cleared->predicted_ns == 1040000000LL && cleared->values[3] == 0,
                 "menu-capture pad clear preserves the current XR frame identity");
          menu_capture = false;
          left_stick_x = 0;
          right_trigger = 0;
          dsvr::InputPollFrame(base, kHead, 5, 1050000000LL);
          left_stick_x = 0.5f;
          dsvr::InputPollFrame(base, kHead, 5, 1060000000LL);
          sync_result = XR_SESSION_NOT_FOCUSED;
          dsvr::InputPollFrame(base, kHead, 5, 1070000000LL);
          cleared = last_pad();
          Expect(cleared != trace_events.rend() && cleared->flags == 0 && cleared->epoch == 5 &&
                 cleared->predicted_ns == 1070000000LL,
                 "focus-loss pad clear preserves the current XR frame identity");
          const auto invalid_hand = std::find_if(trace_events.rbegin(), trace_events.rend(),
              [](const auto& event) {
                return std::strcmp(event.event, "hand_tracking") == 0 && event.values[0] == 0.0f;
              });
          Expect(invalid_hand != trace_events.rend() && !(invalid_hand->flags & 1u) &&
                 invalid_hand->epoch == 5 && invalid_hand->predicted_ns == 1070000000LL,
                 "focus-loss hand invalidation preserves available XR frame identity");
          sync_result = XR_SUCCESS;
      trace_active = false;
      right_pose_valid = true;
      right_trigger = 0;
      left_stick_x = 0;
      dsvr::InputInvalidateFrame();
      config = saved;
    }
    {
      const auto saved=config;
      const auto saved_status=dsvr::g_combat_status;
      const bool saved_pose=right_pose_valid;
      config.arm_ik_enabled=true;
      right_stick_value=right_trigger=right_squeeze=0;
      dsvr::InputInvalidateFrame();
      dsvr::InputPollFrame(reinterpret_cast<XrSpace>(100),kHead,1,1000000000LL);
      right_trigger=right_squeeze=1;
      for(bool observe_only:{false,true}) {
        config.arm_ik_log_only=observe_only;
        dsvr::InputPollFrame(reinterpret_cast<XrSpace>(100),kHead,1,1010000000LL);
        dsvr::VirtualPadState pad{};
        Expect(dsvr::InputLatestPad(pad) && pad.right_trigger==255 && (pad.buttons&dsvr::kPadRightShoulder),
            "IK preference cannot disable gameplay combat buttons while native physical combat is unbound");
      }
      config.arm_ik_log_only=false;
      config.physical_attacks=config.physical_defence=false;
      right_pose_valid=false;
      right_primary=true;
      for (bool dominant_left : {false, true}) {
        config.dominant_hand_left=dominant_left;
        for (int bound=0; bound<3; ++bound) {
          dsvr::g_combat_status.physical_damage_bound=bound==1;
          dsvr::g_combat_status.shield_contact_bound=bound==2;
          dsvr::InputPollFrame(reinterpret_cast<XrSpace>(100),kHead,1,
                              1020000000LL+bound*10000000LL);
          dsvr::VirtualPadState pad{};
          dsvr::InputLatestPad(pad);
          Expect((pad.buttons & dsvr::kPadA) &&
                 (pad.buttons & (dominant_left ? dsvr::kPadLeftShoulder : dsvr::kPadRightShoulder)) &&
                 (dominant_left ? pad.left_trigger : pad.right_trigger)==255,
                 "explicit interact, attack, block and parry survive pose loss and incomplete native binding");
        }
      }
      dsvr::g_combat_status=saved_status;
      right_pose_valid=saved_pose;
      right_primary=false;
      config=saved;right_trigger=right_squeeze=0;right_stick_value=1;
      dsvr::InputInvalidateFrame();
    }
    {
      const auto base=reinterpret_cast<XrSpace>(100);
      dsvr::HandSample hands[2];dsvr::ArmTarget arms[2];
      dsvr::InputPollFrame(base,kHead,1,1000000000LL);
      dsvr::InputCopyTrackingSamples(hands,arms);
      Expect(hands[1].aim_valid && std::fabs(hands[1].aim_quat[0]-.258819045f)<1e-5f &&
          hands[1].quat[0]==0,"aim and grip orientations stay independent");
      right_pose_valid=false;right_trigger=1;
      dsvr::InputPollFrame(base,kHead,1,1010000000LL);
      dsvr::InputCopyTrackingSamples(hands,arms);
      Expect(!hands[1].valid && hands[1].aim_valid && hands[1].trigger_held,
          "grip loss preserves valid pointer aim and select buttons");
      right_pose_valid=true;aim_active=false;
      dsvr::InputPollFrame(base,kHead,1,1020000000LL);
      dsvr::InputCopyTrackingSamples(hands,arms);
      Expect(hands[1].valid && !hands[1].aim_valid,"inactive aim cannot become a stale pointer");
      aim_active=true;right_trigger=0;
      dsvr::InputInvalidateFrame();dsvr::InputCopyTrackingSamples(hands,arms);
      Expect(!hands[1].aim_valid,"invalidation clears the pointer as well as grip");
    }
    TheaterButtons();
    FailedTurnQuery(false);
    FailedTurnQuery(true);
    FailedFirstTurnQuery(false);
    FailedFirstTurnQuery(true);
    sanctuary=true;left_stick_y=-.9f;
    dsvr::InputInvalidateFrame();
    dsvr::InputPollFrame(reinterpret_cast<XrSpace>(100), kHead, 1,1900000000);
    dsvr::VirtualPadState menu_pad{};
    Expect(dsvr::InputLatestPad(menu_pad) && (menu_pad.buttons&dsvr::kPadDpadDown) &&
           menu_pad.thumb_ly==0,"native theater menus receive D-pad navigation from the left stick");
    sanctuary=false;left_stick_y=0;
    left_stick_x=-.4f;left_stick_y=-.9f;
    dsvr::InputInvalidateFrame();
    dsvr::InputPublishHeadDirectedLocomotion({0, 0, 1}, {0, 0, 1}, 1);
    dsvr::InputPollFrame(reinterpret_cast<XrSpace>(100), kHead, 1,1910000000);
    Expect(dsvr::InputLatestPad(menu_pad) &&
           !(menu_pad.buttons&(dsvr::kPadDpadUp|dsvr::kPadDpadDown|
                               dsvr::kPadDpadLeft|dsvr::kPadDpadRight)) &&
           menu_pad.thumb_lx<0 && menu_pad.thumb_ly<0,
           "gameplay preserves analog left-stick movement instead of menu D-pad conversion");
    left_stick_x=0;left_stick_y=0;
    dsvr::InputInvalidateFrame();
    HeadDirectedLocomotion();
      MovementReferenceFrames();
      MovementAlwaysOriented();
    ReferenceChords();
    CalibratedRoomscale();
    PhysicalAttacks();
    BridgeReadinessLog();
    EquipmentReprime();
    PhysicalDefence();
    UntargetedReachRefuses();
    StaleSelectionNeverTaps();
    FreshSelectionStillRefusesWithoutEligibility();
    DigitalDpad();
    AnalogDodgeRequest();
        CapturedInput();
    DeferredBindingEdits();
    if (failures != 0) return 1;
    std::puts("input tests passed");
    return 0;
}
