// Preserves the pre-S4.3 full-note scan to verify scheduling without reimplementing DSP expectations.
static void instrument_reference_render(void* userdata, float* interleaved, int frames, uint64_t transport_frame) {
    EngineInstrumentSource* instrument = (EngineInstrumentSource*)userdata;
    if (!instrument || !interleaved || frames <= 0 || instrument->sample_rate <= 0 || instrument->channels <= 0) {
        return;
    }

    const int channels = instrument->channels;
    for (int i = 0; i < frames; ++i) {
        uint64_t global_frame = transport_frame + (uint64_t)i;
        if (global_frame < instrument->timeline_start_frame) {
            continue;
        }
        uint64_t clip_frame = global_frame - instrument->timeline_start_frame;
        if (clip_frame >= instrument->render_duration_frames) {
            continue;
        }

        uint64_t automation_global = global_frame < instrument->export_end_frame ? global_frame : instrument->export_end_frame - 1;
        uint64_t automation_clip = automation_global - instrument->timeline_start_frame;
        EngineInstrumentParams frame_params = instrument_params_at_frame(instrument, automation_global, automation_clip);
        float sample = 0.0f;
        for (int n = 0; n < instrument->note_count; ++n) {
            const EngineMidiNote* note = &instrument->notes[n].note;
            if (clip_frame < note->start_frame) {
                continue;
            }
            uint64_t rel = clip_frame - note->start_frame;
            uint64_t release = instrument_ms_to_frames(frame_params.release_ms, instrument->sample_rate);
            if (rel >= note->duration_frames && rel - note->duration_frames >= release) continue;
            double freq = midi_note_frequency(note->note);
            if (instrument->preset == ENGINE_INSTRUMENT_PRESET_SYNTH_LAB &&
                frame_params.vibrato_depth > 0.0f &&
                frame_params.vibrato_rate > 0.0f) {
                double seconds = (double)rel / (double)instrument->sample_rate;
                double cents = sin(2.0 * M_PI * (double)frame_params.vibrato_rate * seconds) *
                               (double)frame_params.vibrato_depth;
                freq *= pow(2.0, cents / 1200.0);
            }
            double phase = (2.0 * M_PI * freq * (double)rel) / (double)instrument->sample_rate;
            float env = note_envelope(rel, note->duration_frames, &frame_params, instrument->sample_rate);
            sample += preset_sample(instrument->preset, frame_params, phase, freq, instrument->sample_rate) *
                      frame_params.level *
                      note->velocity *
                      env;
        }

        if (sample > 1.0f) {
            sample = 1.0f;
        } else if (sample < -1.0f) {
            sample = -1.0f;
        }
        float volume = instrument_signal_lane(instrument->automation_lanes, instrument->automation_lane_count,
            ENGINE_AUTOMATION_TARGET_VOLUME, automation_clip, instrument->clip_duration_frames);
        volume += instrument_signal_lane(instrument->track_automation_lanes, instrument->track_automation_lane_count,
            ENGINE_AUTOMATION_TARGET_VOLUME, automation_global, UINT64_MAX);
        float pan = instrument_signal_lane(instrument->automation_lanes, instrument->automation_lane_count,
            ENGINE_AUTOMATION_TARGET_PAN, automation_clip, instrument->clip_duration_frames);
        pan += instrument_signal_lane(instrument->track_automation_lanes, instrument->track_automation_lane_count,
            ENGINE_AUTOMATION_TARGET_PAN, automation_global, UINT64_MAX);
        sample *= fminf(2, fmaxf(0, 1+volume));
        pan = fminf(1, fmaxf(-1, pan));
        for (int ch = 0; ch < channels; ++ch) {
            float balance = channels < 2 ? 1 : ch==0 && pan>0 ? 1-pan : ch==1 && pan<0 ? 1+pan : 1;
            interleaved[i * channels + ch] = sample * balance;
        }
    }
}
