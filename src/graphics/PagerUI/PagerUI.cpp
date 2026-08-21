#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

#include "PagerUI.h"

#include "DebugConfiguration.h"
#include "Display/LGFX_TLoraPager.h"
#include "Display/LvglPort.h"
#include "NodeDB.h"
#include "Screens/StatusBar.h"
#include "Theme.h"
#include "UiTask.h"

#include <lvgl.h>

namespace PagerUI
{

static StatusBar statusBar;
static lv_obj_t *homeScreen = nullptr;

void preInit()
{
    // The rotary encoder does not emit fixed events: RotaryEncoderImpl::init() reads the event
    // codes out of moduleConfig.canned_message at construction time, and InputBroker::Init()
    // runs that constructor. The stored T-Lora Pager defaults are USER_PRESS / ALT_PRESS
    // (NodeDB::installDefaultModuleConfig), which BaseUI interprets as scroll but which carry
    // no directional meaning of their own.
    //
    // PagerUI wants the wheel to mean up/down, and wants USER_PRESS left free for the front
    // button. moduleConfig is persisted, so setting a different compiled default would not
    // help an already-provisioned device - force the values on every boot instead. This is
    // deliberately scoped to the pager's own build so the stock BaseUI env keeps its bindings.
    auto &cm = moduleConfig.canned_message;
    const auto wantCw = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_DOWN;
    const auto wantCcw = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_UP;
    const auto wantPress = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT;

    if (cm.inputbroker_event_cw != wantCw || cm.inputbroker_event_ccw != wantCcw || cm.inputbroker_event_press != wantPress) {
        LOG_INFO("PagerUI: rebinding rotary to DOWN/UP/SELECT (was 0x%x/0x%x/0x%x)", cm.inputbroker_event_cw,
                 cm.inputbroker_event_ccw, cm.inputbroker_event_press);
        cm.inputbroker_event_cw = wantCw;
        cm.inputbroker_event_ccw = wantCcw;
        cm.inputbroker_event_press = wantPress;
    }

    // Belt and braces: NodeDB::loadFromDisk() already clamps this, but preInit() is also the
    // gate in front of InputBroker::Init(), and a COLOR displaymode there costs us both the
    // keyboard and the wheel with nothing in the log to explain it.
    nodeDB->clampDisplayModeForBuild();
}

/// Placeholder home screen. Replaced by the conversation list in a later milestone; for now
/// it exists to prove the panel, LVGL, the theme and the status bar are all alive.
static void buildHomeScreen()
{
    const auto &p = Theme::palette();

    homeScreen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(homeScreen);
    lv_obj_set_size(homeScreen, kScreenWidth, kScreenHeight);
    lv_obj_set_style_bg_color(homeScreen, p.background, 0);
    lv_obj_set_style_bg_opa(homeScreen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(homeScreen, LV_OBJ_FLAG_SCROLLABLE);

    statusBar.attach(homeScreen);
    statusBar.setTitle("Chats");

    // Avatar chip, using the same treatment every contact row will get.
    const uint32_t me = nodeDB->getNodeNum();
    lv_obj_t *avatar = lv_obj_create(homeScreen);
    lv_obj_remove_style_all(avatar);
    lv_obj_set_size(avatar, Theme::kAvatarSize, Theme::kAvatarSize);
    lv_obj_align(avatar, LV_ALIGN_LEFT_MID, Theme::kPadding, 0);
    lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(avatar, Theme::colorForNode(me), 0);
    lv_obj_set_style_bg_opa(avatar, LV_OPA_COVER, 0);
    lv_obj_remove_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *initials = lv_label_create(avatar);
    lv_obj_set_style_text_font(initials, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(initials, lv_color_white(), 0);
    lv_label_set_text(initials, owner.short_name);
    lv_obj_center(initials);

    lv_obj_t *name = lv_label_create(homeScreen);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name, p.text, 0);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, Theme::kPadding * 2 + Theme::kAvatarSize, -8);
    lv_label_set_text(name, owner.long_name);

    lv_obj_t *sub = lv_label_create(homeScreen);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sub, p.textDim, 0);
    lv_obj_align(sub, LV_ALIGN_LEFT_MID, Theme::kPadding * 2 + Theme::kAvatarSize, 10);
    lv_label_set_text(sub, "PagerUI is up. No conversations yet.");

    lv_screen_load(homeScreen);
}

static void tickCb(lv_timer_t *)
{
    statusBar.refresh();
}

void setup()
{
    if (!LvglPort::begin()) {
        LOG_ERROR("PagerUI: display bring-up failed; UI disabled");
        return;
    }

    // These lv_* calls run on the main task, which only stays within the "UI task owns LVGL"
    // rule because the UI task does not exist yet. Everything after UiTask::start() below
    // must go through a queue instead.
    buildHomeScreen();
    lv_timer_create(tickCb, 1000, nullptr);

    if (!UiTask::start())
        return;

    // Only light the panel once there is something on it, so boot does not flash a white
    // or garbage frame.
    LvglPort::setBrightness(BRIGHTNESS_DEFAULT);

    LOG_INFO("PagerUI: ready");
}

} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
