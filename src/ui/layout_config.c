#include "ui/layout_config.h"

static const UILayoutConfig kDefaultConfig = {
    .transport_height_ratio = 0.12f,
    .mixer_height_ratio = 0.28f,
    .library_width_ratio = 0.22f,
    .min_transport_height = 56,
    .min_mixer_height = 140,
    .min_timeline_height = 220,
    .min_library_width = 200
};

const UILayoutConfig* ui_layout_config_get(void) {
    return &kDefaultConfig;
}
