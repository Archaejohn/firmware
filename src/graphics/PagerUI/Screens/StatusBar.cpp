#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

#include "StatusBar.h"

#include "PowerStatus.h"
#include "gps/RTC.h"
#include "graphics/PagerUI/Theme.h"

#include <stdio.h>

namespace PagerUI
{

void StatusBar::attach(lv_obj_t *parent)
{
    const auto &p = Theme::palette();

    root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), Theme::kStatusBarHeight);
    lv_obj_align(root, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(root, p.surface, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(root, Theme::kPadding, 0);
    lv_obj_set_style_border_side(root, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(root, p.divider, 0);
    lv_obj_set_style_border_width(root, 1, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    clockLabel = lv_label_create(root);
    lv_obj_set_style_text_font(clockLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(clockLabel, p.text, 0);
    lv_obj_align(clockLabel, LV_ALIGN_LEFT_MID, 0, 0);
    lv_label_set_text(clockLabel, "--:--");

    titleLabel = lv_label_create(root);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(titleLabel, p.textDim, 0);
    lv_obj_align(titleLabel, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(titleLabel, "");

    unreadLabel = lv_label_create(root);
    lv_obj_set_style_text_font(unreadLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(unreadLabel, p.accentBright, 0);
    lv_obj_align(unreadLabel, LV_ALIGN_RIGHT_MID, -46, 0);
    lv_obj_add_flag(unreadLabel, LV_OBJ_FLAG_HIDDEN);

    batteryLabel = lv_label_create(root);
    lv_obj_set_style_text_font(batteryLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(batteryLabel, p.textDim, 0);
    lv_obj_align(batteryLabel, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_label_set_text(batteryLabel, "--");
}

void StatusBar::setTitle(const char *title)
{
    if (titleLabel)
        lv_label_set_text(titleLabel, title ? title : "");
}

void StatusBar::setUnread(uint16_t count)
{
    unread = count;
    if (!unreadLabel)
        return;

    if (count == 0) {
        lv_obj_add_flag(unreadLabel, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    char buf[16];
    snprintf(buf, sizeof(buf), LV_SYMBOL_BELL " %u", (unsigned)(count > 99 ? 99 : count));
    lv_label_set_text(unreadLabel, buf);
    lv_obj_remove_flag(unreadLabel, LV_OBJ_FLAG_HIDDEN);
}

void StatusBar::refresh()
{
    if (!root)
        return;

    // Clock. Anything below RTCQualityDevice is a guess, so show placeholder dashes rather
    // than a confidently wrong time.
    if (clockLabel) {
        const uint32_t now = getValidTime(RTCQualityDevice, true);
        if (now == 0) {
            lv_label_set_text(clockLabel, "--:--");
        } else {
            const uint32_t secsToday = now % 86400;
            char buf[8];
            snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(secsToday / 3600), (unsigned)((secsToday % 3600) / 60));
            lv_label_set_text(clockLabel, buf);
        }
    }

    if (batteryLabel && powerStatus) {
        const auto &p = Theme::palette();

        if (!powerStatus->getHasBattery()) {
            lv_obj_set_style_text_color(batteryLabel, p.textDim, 0);
            lv_label_set_text(batteryLabel, LV_SYMBOL_USB);
        } else {
            const uint8_t pct = powerStatus->getBatteryChargePercent();
            const bool charging = powerStatus->getIsCharging();
            char buf[16];
            snprintf(buf, sizeof(buf), "%s %u%%", charging ? LV_SYMBOL_CHARGE : LV_SYMBOL_BATTERY_FULL, (unsigned)pct);
            lv_obj_set_style_text_color(batteryLabel, charging ? p.ok : (pct <= 15 ? p.error : p.textDim), 0);
            lv_label_set_text(batteryLabel, buf);
        }
    }
}

} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
