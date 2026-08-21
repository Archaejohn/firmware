#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

#include "PagerUI.h"

#include "DebugConfiguration.h"
#include "NodeDB.h"

namespace PagerUI
{

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

void setup()
{
    LOG_INFO("PagerUI: setup (display %dx%d)", TFT_HEIGHT, TFT_WIDTH);
}

} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
