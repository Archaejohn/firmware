#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

#include <lvgl.h>

namespace PagerUI
{

/// The persistent 22px strip across the top of every screen: clock, title, unread count and
/// battery. Built once per screen and refreshed on a timer from the UI task.
class StatusBar
{
  public:
    /// Create the bar as a child of `parent`, pinned to its top edge.
    void attach(lv_obj_t *parent);

    /// Screen name shown in the middle of the bar.
    void setTitle(const char *title);

    /// Total unread across all conversations; 0 hides the badge.
    void setUnread(uint16_t count);

    /// Pull clock and battery from the device and redraw. Cheap enough to call once a second.
    void refresh();

  private:
    lv_obj_t *root = nullptr;
    lv_obj_t *clockLabel = nullptr;
    lv_obj_t *titleLabel = nullptr;
    lv_obj_t *unreadLabel = nullptr;
    lv_obj_t *batteryLabel = nullptr;
    uint16_t unread = 0;
};

} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
