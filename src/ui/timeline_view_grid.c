#include "ui/timeline_view_grid.h"

#include "ui/font.h"

#include <math.h>
#include <stdio.h>

void timeline_view_draw_grid(SDL_Renderer* renderer,
                             int x0,
                             int width,
                             int top,
                             int height,
                             float pixels_per_second,
                             float visible_seconds,
                             bool show_all_lines,
                             float window_start_seconds,
                             bool view_in_beats,
                             const TempoMap* tempo_map,
                             const TimeSignatureMap* signature_map) {
    SDL_SetRenderDrawColor(renderer, 60, 60, 72, 255);
    SDL_Rect border = {x0, top, width, height};
    SDL_RenderDrawRect(renderer, &border);

    SDL_Color label_color = {150, 150, 160, 255};
    SDL_Color minor_line = {65, 65, 85, 255};
    SDL_Color sub_line = {66, 70, 100, 200};
    SDL_Color major_line = {80, 82, 115, 255};
    SDL_Color downbeat_line = {90, 100, 130, 255};
    int label_scale = 1;
    int label_h = ui_font_line_height((float)label_scale);
    int label_y = top - label_h - (label_h / 4 > 2 ? label_h / 4 : 2);
    int min_label_gap = (label_h / 3 > 4) ? (label_h / 3) : 4;
    SDL_SetRenderDrawColor(renderer, minor_line.r, minor_line.g, minor_line.b, minor_line.a);

    if (!view_in_beats || !tempo_map || tempo_map->event_count <= 0 || tempo_map->sample_rate <= 0.0) {
        if (show_all_lines) {
            float first_minor = floorf(window_start_seconds);
            float last_minor = window_start_seconds + visible_seconds;
            SDL_SetRenderDrawColor(renderer, minor_line.r, minor_line.g, minor_line.b, minor_line.a);
            for (float sec_abs = first_minor; sec_abs <= last_minor + 0.5f; sec_abs += 1.0f) {
                float local = sec_abs - window_start_seconds;
                if (local < 0.0f || local > visible_seconds * 1.5f) {
                    continue;
                }
                int x = x0 + (int)roundf(local * pixels_per_second);
                SDL_RenderDrawLine(renderer, x, top, x, top + height);
            }
        }

        float major_interval = 1.0f;
        if (visible_seconds > 60.0f) {
            major_interval = 10.0f;
        } else if (visible_seconds > 30.0f) {
            major_interval = 5.0f;
        } else if (visible_seconds > 15.0f) {
            major_interval = 2.0f;
        }

        float first_major_sec = floor(window_start_seconds / major_interval) * major_interval;
        float last_major_sec = window_start_seconds + visible_seconds + major_interval * 0.5f;
        int last_label_right = x0 - 4096;
        for (float sec_abs = first_major_sec; sec_abs <= last_major_sec; sec_abs += major_interval) {
            float local = sec_abs - window_start_seconds;
            if (local < 0.0f || local > visible_seconds * 1.5f) {
                continue;
            }
            int x = x0 + (int)roundf(local * pixels_per_second);
            SDL_SetRenderDrawColor(renderer, major_line.r, major_line.g, major_line.b, major_line.a);
            SDL_RenderDrawLine(renderer, x, top, x, top + height);

            float label_sec = sec_abs;
            int total_seconds = (int)label_sec;
            int minutes = total_seconds / 60;
            int seconds = total_seconds % 60;
            char label[16];
            snprintf(label, sizeof(label), "%02d:%02d", minutes, seconds);
            int label_x = x + 4;
            int label_w = ui_measure_text_width(label, (float)label_scale);
            if (label_x <= last_label_right + min_label_gap) {
                continue;
            }
            ui_draw_text(renderer, label_x, label_y, label, label_color, (float)label_scale);
            last_label_right = label_x + label_w;
        }
        return;
    }

    double start_beats = tempo_map_seconds_to_beats(tempo_map, (double)window_start_seconds);
    double end_beats = tempo_map_seconds_to_beats(tempo_map, (double)(window_start_seconds + visible_seconds));
    double visible_beats = end_beats - start_beats;
    if (visible_beats < 0.0001) {
        visible_beats = 0.0001;
    }

    if (show_all_lines) {
        const TimeSignatureEvent* minor_sig = signature_map ? time_signature_map_event_at_beat(signature_map, start_beats) : NULL;
        double beat_unit = time_signature_beat_unit(minor_sig);
        if (beat_unit <= 0.0) {
            beat_unit = 1.0;
        }
        double first_minor = floor(start_beats / beat_unit) * beat_unit;
        double last_minor = end_beats;
        SDL_SetRenderDrawColor(renderer, minor_line.r, minor_line.g, minor_line.b, minor_line.a);
        for (double gb = first_minor; gb <= last_minor + beat_unit * 0.5; gb += beat_unit) {
            double sec = tempo_map_beats_to_seconds(tempo_map, gb);
            double local_sec = sec - window_start_seconds;
            if (local_sec < 0.0 || local_sec > visible_seconds * 1.5) {
                continue;
            }
            int x = x0 + (int)round(local_sec * (double)pixels_per_second);
            SDL_RenderDrawLine(renderer, x, top, x, top + height);
        }
    }

    const TimeSignatureEvent* base_sig = signature_map ? time_signature_map_event_at_beat(signature_map, start_beats) : NULL;
    double beat_unit = time_signature_beat_unit(base_sig);
    if (beat_unit <= 0.0) {
        beat_unit = 1.0;
    }
    double beats_per_bar = time_signature_beats_per_bar(base_sig);
    if (beats_per_bar <= 0.0) {
        beats_per_bar = 4.0;
    }
    double major_units = 1.0;
    double visible_units = (beat_unit > 0.0) ? (visible_beats / beat_unit) : visible_beats;
    if (visible_units > 128.0) {
        major_units = 8.0;
    } else if (visible_units > 64.0) {
        major_units = 4.0;
    } else if (visible_units > 32.0) {
        major_units = 2.0;
    }
    double major_interval_beats = beat_unit * major_units;

    double sub_interval = 0.0;
    if (visible_beats <= 8.0) {
        sub_interval = time_signature_beat_unit(base_sig) * 0.25;
    } else if (visible_beats <= 32.0) {
        sub_interval = time_signature_beat_unit(base_sig) * 0.5;
    }
    if (sub_interval > 0.0) {
        SDL_SetRenderDrawColor(renderer, sub_line.r, sub_line.g, sub_line.b, sub_line.a);
        double first_sub = floor(start_beats / sub_interval) * sub_interval;
        double last_sub = end_beats + sub_interval * 0.5;
        for (double gb = first_sub; gb <= last_sub; gb += sub_interval) {
            double sec = tempo_map_beats_to_seconds(tempo_map, gb);
            double local_sec = sec - window_start_seconds;
            if (local_sec < 0.0 || local_sec > visible_seconds * 1.5) {
                continue;
            }
            int x = x0 + (int)round(local_sec * (double)pixels_per_second);
            SDL_RenderDrawLine(renderer, x, top, x, top + height);
        }
    }

    if (!show_all_lines) {
        double visible_bars = visible_beats / (double)beats_per_bar;
        int bar_interval = 1;
        if (visible_bars > 128.0) {
            bar_interval = 16;
        } else if (visible_bars > 64.0) {
            bar_interval = 8;
        } else if (visible_bars > 32.0) {
            bar_interval = 4;
        } else if (visible_bars > 16.0) {
            bar_interval = 2;
        }

        int bar = 1;
        int beat_idx = 1;
        double sub = 0.0;
        time_signature_map_beat_to_bar_beat(signature_map, start_beats, &bar, &beat_idx, &sub, NULL, NULL);
        double bar_start = start_beats - (((double)(beat_idx - 1) + sub) * beat_unit);
        if (bar_start < 0.0) {
            bar_start = 0.0;
            bar = 1;
        }
        int last_label_right = x0 - 4096;
        while (bar_start <= end_beats + 0.5) {
            if ((bar - 1) % bar_interval == 0) {
                double sec = tempo_map_beats_to_seconds(tempo_map, bar_start);
                double local_sec = sec - window_start_seconds;
                if (local_sec >= 0.0 && local_sec <= visible_seconds * 1.5) {
                    int x = x0 + (int)round(local_sec * (double)pixels_per_second);
                    SDL_SetRenderDrawColor(renderer, downbeat_line.r, downbeat_line.g, downbeat_line.b, downbeat_line.a);
                    SDL_RenderDrawLine(renderer, x, top, x, top + height);

                    char label[24];
                    snprintf(label, sizeof(label), "%d", bar);
                    int label_x = x + 4;
                    int label_w = ui_measure_text_width(label, (float)label_scale);
                    if (label_x > last_label_right + min_label_gap) {
                        ui_draw_text(renderer, label_x, label_y, label, label_color, (float)label_scale);
                        last_label_right = label_x + label_w;
                    }
                }
            }
            const TimeSignatureEvent* sig = time_signature_map_event_at_beat(signature_map, bar_start + 1e-6);
            double next_beats_per_bar = time_signature_beats_per_bar(sig);
            if (next_beats_per_bar <= 0.0) {
                next_beats_per_bar = beats_per_bar;
            }
            bar_start += next_beats_per_bar;
            bar += 1;
        }
        return;
    }

    double first_major = floor(start_beats / major_interval_beats) * major_interval_beats;
    double last_major = end_beats + major_interval_beats * 0.5;
    int last_label_right = x0 - 4096;
    for (double gb = first_major; gb <= last_major; gb += major_interval_beats) {
        double sec = tempo_map_beats_to_seconds(tempo_map, gb);
        double local_sec = sec - window_start_seconds;
        if (local_sec < 0.0 || local_sec > visible_seconds * 1.5) {
            continue;
        }
        int x = x0 + (int)round(local_sec * (double)pixels_per_second);
        int bar = 1;
        int beat_idx = 1;
        double sub = 0.0;
        time_signature_map_beat_to_bar_beat(signature_map, gb, &bar, &beat_idx, &sub, NULL, NULL);
        bool is_downbeat = beat_idx == 1;
        if (is_downbeat) {
            SDL_SetRenderDrawColor(renderer, downbeat_line.r, downbeat_line.g, downbeat_line.b, downbeat_line.a);
        } else {
            SDL_SetRenderDrawColor(renderer, major_line.r, major_line.g, major_line.b, major_line.a);
        }
        SDL_RenderDrawLine(renderer, x, top, x, top + height);

        char label[24];
        snprintf(label, sizeof(label), "%d.%d", bar, beat_idx);
        int label_x = x + 4;
        int label_w = ui_measure_text_width(label, (float)label_scale);
        if (label_x <= last_label_right + min_label_gap) {
            continue;
        }
        SDL_Color c = label_color;
        if (beat_idx == 1) {
            c = (SDL_Color){200, 210, 230, 255};
        }
        ui_draw_text(renderer, label_x, label_y, label, c, (float)label_scale);
        last_label_right = label_x + label_w;
    }
}

void timeline_view_format_label(float seconds,
                                bool view_in_beats,
                                const TempoMap* tempo_map,
                                const TimeSignatureMap* signature_map,
                                char* out,
                                size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (view_in_beats && tempo_map && tempo_map->event_count > 0 && tempo_map->sample_rate > 0.0) {
        double beats = tempo_map_seconds_to_beats(tempo_map, seconds);
        int bar = 1;
        int beat_idx = 1;
        double sub = 0.0;
        time_signature_map_beat_to_bar_beat(signature_map, beats, &bar, &beat_idx, &sub, NULL, NULL);
        if (bar < 1) bar = 1;
        if (beat_idx < 1) beat_idx = 1;
        snprintf(out, out_len, "%d.%02d", bar, beat_idx);
    } else {
        int total_ms = (int)llroundf(seconds * 1000.0f);
        int minutes = total_ms / 60000;
        int seconds_part = (total_ms / 1000) % 60;
        int millis = total_ms % 1000;
        snprintf(out, out_len, "%02d:%02d.%03d", minutes, seconds_part, millis);
    }
    out[out_len - 1] = '\0';
}
