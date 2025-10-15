#pragma once

typedef struct {
    float transport_height_ratio;   // fraction of window height reserved for transport
    float mixer_height_ratio;       // fraction of remaining height given to mixer
    float library_width_ratio;      // fraction of content width given to library

    int min_transport_height;
    int min_mixer_height;
    int min_timeline_height;
    int min_library_width;
} UILayoutConfig;

const UILayoutConfig* ui_layout_config_get(void);
