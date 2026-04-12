#include "session_io_read_internal.h"

#include <string.h>

bool parse_session_document_effects_panel(JsonReader* r, SessionDocument* doc) {
    if (!json_expect(r, '{')) {
        return false;
    }
    while (true) {
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == '}') {
            ++r->pos;
            break;
        }
        char panel_key[64];
        if (!json_parse_string(r, panel_key, sizeof(panel_key))) {
            return false;
        }
        if (!json_expect(r, ':')) {
            return false;
        }
        if (strcmp(panel_key, "view_mode") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.view_mode = (int)val;
        } else if (strcmp(panel_key, "selected_index") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.selected_index = (int)val;
        } else if (strcmp(panel_key, "open_index") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.open_index = (int)val;
        } else if (strcmp(panel_key, "list_detail_mode") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.list_detail_mode = (int)val;
        } else if (strcmp(panel_key, "eq_view_mode") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.eq_view_mode = (int)val;
        } else if (strcmp(panel_key, "meter_scope_mode") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.meter_scope_mode = (int)val;
        } else if (strcmp(panel_key, "meter_lufs_mode") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.meter_lufs_mode = (int)val;
        } else if (strcmp(panel_key, "meter_spectrogram_mode") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.meter_spectrogram_mode = (int)val;
        } else if (strcmp(panel_key, "low_cut_enabled") == 0) {
            if (!json_parse_bool(r, &doc->effects_panel.low_cut.enabled)) {
                return false;
            }
        } else if (strcmp(panel_key, "low_cut_freq") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.low_cut.freq_hz = (float)val;
        } else if (strcmp(panel_key, "low_cut_slope") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.low_cut.slope = (float)val;
        } else if (strcmp(panel_key, "high_cut_enabled") == 0) {
            if (!json_parse_bool(r, &doc->effects_panel.high_cut.enabled)) {
                return false;
            }
        } else if (strcmp(panel_key, "high_cut_freq") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.high_cut.freq_hz = (float)val;
        } else if (strcmp(panel_key, "high_cut_slope") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.high_cut.slope = (float)val;
        } else if (strcmp(panel_key, "band_0_enabled") == 0) {
            if (!json_parse_bool(r, &doc->effects_panel.bands[0].enabled)) {
                return false;
            }
        } else if (strcmp(panel_key, "band_0_freq") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.bands[0].freq_hz = (float)val;
        } else if (strcmp(panel_key, "band_0_gain") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.bands[0].gain_db = (float)val;
        } else if (strcmp(panel_key, "band_0_q") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.bands[0].q_width = (float)val;
        } else if (strcmp(panel_key, "band_1_enabled") == 0) {
            if (!json_parse_bool(r, &doc->effects_panel.bands[1].enabled)) {
                return false;
            }
        } else if (strcmp(panel_key, "band_1_freq") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.bands[1].freq_hz = (float)val;
        } else if (strcmp(panel_key, "band_1_gain") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.bands[1].gain_db = (float)val;
        } else if (strcmp(panel_key, "band_1_q") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.bands[1].q_width = (float)val;
        } else if (strcmp(panel_key, "band_2_enabled") == 0) {
            if (!json_parse_bool(r, &doc->effects_panel.bands[2].enabled)) {
                return false;
            }
        } else if (strcmp(panel_key, "band_2_freq") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.bands[2].freq_hz = (float)val;
        } else if (strcmp(panel_key, "band_2_gain") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.bands[2].gain_db = (float)val;
        } else if (strcmp(panel_key, "band_2_q") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.bands[2].q_width = (float)val;
        } else if (strcmp(panel_key, "band_3_enabled") == 0) {
            if (!json_parse_bool(r, &doc->effects_panel.bands[3].enabled)) {
                return false;
            }
        } else if (strcmp(panel_key, "band_3_freq") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.bands[3].freq_hz = (float)val;
        } else if (strcmp(panel_key, "band_3_gain") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.bands[3].gain_db = (float)val;
        } else if (strcmp(panel_key, "band_3_q") == 0) {
            double val;
            if (!json_parse_number(r, &val)) {
                return false;
            }
            doc->effects_panel.bands[3].q_width = (float)val;
        } else {
            if (!json_skip_value(r)) {
                return false;
            }
        }
        json_skip_whitespace(r);
        if (r->pos < r->length && r->data[r->pos] == ',') {
            ++r->pos;
            continue;
        }
        if (r->pos < r->length && r->data[r->pos] == '}') {
            ++r->pos;
            break;
        }
        return false;
    }
    return true;
}
