// Fake-runtime regression seam linked against the actual core/input.cpp.
#include "../src/core/input.h"
#include "../src/core/diagnostics.h"
#include "../src/common/logging.h"
#include <openxr/openxr.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
const float kHead[3] = {0.0f, 1.6f, 0.0f};
std::uintptr_t next_handle = 1;
XrPath left_path = XR_NULL_PATH;
XrAction stick_action = XR_NULL_HANDLE;
XrAction squeeze_action = XR_NULL_HANDLE;
XrAction trigger_action = XR_NULL_HANDLE;
XrAction menu_action = XR_NULL_HANDLE;
XrAction primary_action = XR_NULL_HANDLE;
XrAction secondary_action = XR_NULL_HANDLE;
XrAction stick_click_action = XR_NULL_HANDLE;
XrSpace view_space = XR_NULL_HANDLE;
XrResult turn_result = XR_SUCCESS;
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
bool right_primary = false;
bool right_secondary = false;
bool left_stick_click = false;
bool right_stick_click = false;
bool left_pose_valid = true;
float head_quat[4] = {0, 0, 0, 1};
float left_quat[4] = {0, 0, 0, 1};
float right_quat[4] = {0, 0, 0, 1};
int calibration_requests = 0;
int recenter_calls = 0;
bool sanctuary = false;
float left_stick_x = 0;
float left_stick_y = 0;

template <typename T> T Handle() {
    return reinterpret_cast<T>(next_handle++);
}

void Expect(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
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

    dsvr::InputInvalidateFrame();
    dsvr::InputPollFrame(base, kHead, 1, 1000000000);
    dsvr::InputPollFrame(base, kHead, 1, 1010000000);
    // Leave the successful frame's yaw undrained, then fail the right-stick query.
    turn_result = XR_ERROR_RUNTIME_FAILURE;
    dsvr::InputPollFrame(base, kHead, 1, 1020000000);
    float yaw = dsvr::InputTakeTurnYaw();
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
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_lx == 0 && pad.thumb_ly == 0,
           "controller mode neutralizes translation when its selected reference is unavailable");
    left_pose_valid = true;
    config.movement_orientation = dsvr::MovementOrientation::Head;

    dsvr::InputPublishHeadDirectedLocomotion({1, 0, 0}, {0, 0, 1}, 2);
    dsvr::InputPollFrame(base, kHead, 1, 1820000000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_lx == 0 && pad.thumb_ly == 0,
           "a locomotion basis from another tracking epoch neutralizes translation");

    dsvr::InputPublishHeadDirectedLocomotion({1, 0, 0}, {0, 0, 1}, 1);
    sanctuary = true;
    dsvr::InputPollFrame(base, kHead, 1, 1830000000);
    Expect(dsvr::InputLatestPad(pad) && (pad.buttons & dsvr::kPadDpadUp) &&
           !(pad.buttons & dsvr::kPadDpadRight),
           "sanctuary navigation keeps raw stick direction");

    sanctuary = false;
    dsvr::InputInvalidateHeadDirectedLocomotion();
    dsvr::InputPollFrame(base, kHead, 1, 1840000000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_lx == 0 && pad.thumb_ly == 0,
           "invalidated locomotion basis neutralizes artificial translation");
    ResetRoomscaleTestControls();
    config.vr_enabled = true;
    config.headlook_enabled = true;
    config.play_style = "roomscale";
    dsvr::InputInvalidateFrame();
    dsvr::RoomscaleBasis room_basis{};
    room_basis.tracking_right = {1, 0, 0};
    room_basis.tracking_up = {0, 1, 0};
    room_basis.tracking_forward = {0, 0, 1};
    room_basis.native_forward = {0, 0, 1};
    room_basis.space_epoch = 1;
    room_basis.valid = true;
    dsvr::InputPublishRoomscaleBasis(room_basis);
    dsvr::InputPublishHeadDirectedLocomotion({0, 0, 1}, {0, 0, 1}, 1);
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
    basis.native_forward = {0, 0, 1};
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
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_lx == 0 && pad.thumb_ly == 0,
           "controller tracking loss immediately neutralizes movement");
    left_pose_valid = true;
    left_quat[0] = 0.0f;
    left_quat[1] = 0.70710678f;
    left_quat[3] = 0.70710678f;
    dsvr::InputPollFrame(base, kHead, 1, 2406800000);
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_lx == 0 && pad.thumb_ly == 0,
           "controller reacquisition requires neutral rearm");
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
    Expect(dsvr::InputLatestPad(pad) && pad.thumb_ly == 0 && pad.thumb_lx == 0,
           "lost selected controller neutralizes movement without HMD fallback");
    left_pose_valid = true;
    left_stick_x = left_stick_y = 0;
    config.movement_orientation = dsvr::MovementOrientation::Head;
    config.dominant_hand_left = false;
    dsvr::InputPollFrame(base, kHead, 1, 2425000000);
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
           menu_selected && !menu_back,
           "Home receives left-stick navigation and trigger-only selection");
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
           menu_selected && !menu_back,
           "Home trigger selection resumes after B/back releases");
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
bool DiagnosticsReloadConfig() { return true; }
void DiagnosticsLogMarker() {}
void XrToggleSanctuary() {}
void XrRecenterSanctuary() { ++recenter_calls; }
bool XrMenuPresentationActive() { return sanctuary; }
bool CalibrationCapturesInput() { return calibration_capture; }
void CalibrationRequest() { ++calibration_requests; calibration_capture = true; }
bool ModMenuCapturesInput() { return menu_capture; }
void ModMenuToggle() { ++menu_toggles; menu_capture = !menu_capture; }
void ModMenuControllerInput(float x, float y, bool select, bool back) {
    menu_x = x;
    menu_y = y;
    menu_selected = select;
    menu_back = back;
}
void LogWrite(LogLevel, const char*, int, const char*, ...) {}
} // namespace dsvr

extern "C" {
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSet(XrInstance, const XrActionSetCreateInfo*, XrActionSet* out) {
    *out = Handle<XrActionSet>();
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrStringToPath(XrInstance, const char* text, XrPath* out) {
    *out = static_cast<XrPath>(next_handle++);
    if (std::strcmp(text, "/user/hand/left") == 0) left_path = *out;
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateAction(XrActionSet, const XrActionCreateInfo* info, XrAction* out) {
    *out = Handle<XrAction>();
    if (std::strcmp(info->actionName, "thumbstick") == 0) stick_action = *out;
    if (std::strcmp(info->actionName, "squeeze") == 0) squeeze_action = *out;
    if (std::strcmp(info->actionName, "trigger") == 0) trigger_action = *out;
    if (std::strcmp(info->actionName, "menu") == 0) menu_action = *out;
    if (std::strcmp(info->actionName, "primary") == 0) primary_action = *out;
    if (std::strcmp(info->actionName, "secondary") == 0) secondary_action = *out;
    if (std::strcmp(info->actionName, "stick_click") == 0) stick_click_action = *out;
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(XrInstance, const XrInteractionProfileSuggestedBinding*) {
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrAttachSessionActionSets(XrSession, const XrSessionActionSetsAttachInfo*) {
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSpace(XrSession, const XrActionSpaceCreateInfo* info, XrSpace* out) {
    *out = reinterpret_cast<XrSpace>(static_cast<std::uintptr_t>(info->subactionPath == left_path ? 200 : 201));
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateReferenceSpace(XrSession, const XrReferenceSpaceCreateInfo*, XrSpace* out) {
    view_space = Handle<XrSpace>();
    *out = view_space;
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrSyncActions(XrSession, const XrActionsSyncInfo*) {
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace space, XrSpace, XrTime, XrSpaceLocation* out) {
    out->locationFlags = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    const float* q = space == view_space ? head_quat :
        (space == reinterpret_cast<XrSpace>(static_cast<std::uintptr_t>(200)) ? left_quat : right_quat);
    if (space == reinterpret_cast<XrSpace>(static_cast<std::uintptr_t>(200)) && !left_pose_valid)
        out->locationFlags &= ~XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    out->pose.orientation = {q[0], q[1], q[2], q[3]};
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateBoolean(XrSession, const XrActionStateGetInfo* info, XrActionStateBoolean* out) {
    out->isActive = XR_TRUE;
    out->currentState = info->action == menu_action && info->subactionPath == left_path && menu_held ? XR_TRUE : XR_FALSE;
    if (info->action == primary_action && info->subactionPath != left_path)
        out->currentState = right_primary ? XR_TRUE : XR_FALSE;
    if (info->action == secondary_action && info->subactionPath != left_path)
        out->currentState = right_secondary ? XR_TRUE : XR_FALSE;
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
    ReferenceChords();
    CapturedInput();
    if (failures != 0) return 1;
    std::puts("input tests passed");
    return 0;
}
