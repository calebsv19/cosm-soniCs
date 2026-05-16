# Keybindings

This doc lists current keyboard shortcuts by pane or mode, with source references.

## Global (all panes)
- `Space` — Play/pause transport. (`src/input/input_manager.c`, `handle_keyboard_shortcuts`)
- `Shift+Space` — Jump to loop start; resume if it was playing. (`src/input/input_manager.c`, `handle_keyboard_shortcuts`)
- `Enter` — Jump to timeline window start. (`src/input/input_manager.c`, `handle_keyboard_shortcuts`)
- `Shift+Enter` — Jump to project start (frame 0). (`src/input/input_manager.c`, `handle_keyboard_shortcuts`)
- `L` — Toggle loop; auto-fills loop end if empty. (`src/input/input_manager.c`, `handle_keyboard_shortcuts`)
- `B` — Bounce (loop range if loop enabled, else entire project). (`src/input/input_manager.c`, `handle_keyboard_shortcuts`)
- `Ctrl/Cmd+B` — Open native folder chooser and set DAW library input root, then rescan. (`src/input/input_manager.c`, `handle_keyboard_shortcuts`; `src/input/library_input.c`)
- `S` — Save session to `<output_root>/last_session.json` (legacy fallback: `config/last_session.json`). (`src/input/input_manager.c`, `handle_keyboard_shortcuts`; `src/session/project_manager.c`)
- `Delete`/`Backspace` — Delete selected clip(s) (when not editing text). (`src/input/input_manager.c`, `handle_keyboard_shortcuts`)
- `F7` / `F8` / `F9` — Toggle engine / cache / timing logs. (`src/input/input_manager.c`, `handle_keyboard_shortcuts`)
- `Ctrl/Cmd+Z` — Undo. (`src/input/input_manager.c`, `handle_keyboard_shortcuts`)
- `Ctrl/Cmd+Shift+Z` — Redo. (`src/input/input_manager.c`, `handle_keyboard_shortcuts`)
- `Ctrl/Cmd+=` / `Ctrl/Cmd++` — Increase UI text size. (`src/input/input_manager.c`, `handle_keyboard_shortcuts`)
- `Ctrl/Cmd+-` — Decrease UI text size. (`src/input/input_manager.c`, `handle_keyboard_shortcuts`)
- `Ctrl/Cmd+0` — Reset UI text size. (`src/input/input_manager.c`, `handle_keyboard_shortcuts`)
- `Ctrl/Cmd+Shift+T` — Cycle shared theme preset forward. (`src/input/input_manager.c`, `handle_keyboard_shortcuts`)
- `Ctrl/Cmd+Shift+Y` — Cycle shared theme preset backward. (`src/input/input_manager.c`, `handle_keyboard_shortcuts`)

Notes:
- Global shortcuts are disabled while editing tempo, library names, or track names. (`src/input/input_manager.c`, `handle_keyboard_shortcuts`)

## Timeline / Arrangement view
- `R` — Arm or finish timeline audio recording on the selected/active track. Capture frames are appended only while transport is playing. (`src/input/timeline/timeline_input_keyboard.c`, `timeline_input_keyboard_handle_event`; `src/app/audio_recording.c`)
- `A` — Toggle automation edit mode. (`src/input/timeline/timeline_input_keyboard.c`, `timeline_input_keyboard_handle_event`)
- `G` — Toggle snap-to-grid. (`src/input/timeline/timeline_input_keyboard.c`, `timeline_input_keyboard_handle_event`)
- `Ctrl/Cmd+C` — Copy selection. (`src/input/timeline/timeline_input_keyboard.c`, `timeline_input_keyboard_handle_event`)
- `Ctrl/Cmd+V` — Paste selection. (`src/input/timeline/timeline_input_keyboard.c`, `timeline_input_keyboard_handle_event`)
- `Ctrl/Cmd+D` — Duplicate selection. (`src/input/timeline/timeline_input_keyboard.c`, `timeline_input_keyboard_handle_event`)

## MIDI editor
- `R` — Toggle QWERTY MIDI note recording for the selected MIDI region. (`src/input/midi_editor_input_qwerty.c`, `midi_editor_handle_qwerty_event`)
- `Z X C V B N M A W S E D F T G Y H U J K` — QWERTY MIDI note keys while record/test capture is active. (`src/input/midi_editor_input_qwerty.c`, `midi_editor_qwerty_note_for_key`)
- `[` / `]` — Move QWERTY MIDI octave down/up. (`src/input/midi_editor_input_qwerty.c`, `midi_editor_handle_qwerty_event`)
- `-` / `=` — Decrease/increase MIDI default velocity. (`src/input/midi_editor_input_qwerty.c`, `midi_editor_handle_qwerty_event`)
- `Q` — Quantize selected MIDI notes. (`src/input/midi_editor_input_qwerty.c`, `midi_editor_handle_qwerty_event`)
- `Delete` / `Backspace` — Delete selected MIDI note(s). (`src/input/midi_editor_input.c`, `midi_editor_input_handle_event`)
- `Ctrl/Cmd+C` / `Ctrl/Cmd+V` / `Ctrl/Cmd+D` — Copy, paste, or duplicate selected MIDI note(s). (`src/input/midi_editor_input_commands.c`, `midi_editor_handle_clipboard_keydown`)
- `Ctrl/Cmd+Left` / `Ctrl/Cmd+Right` — Move selected MIDI note group by the current quantize step. (`src/input/midi_editor_input_commands.c`, `midi_editor_handle_note_command_keydown`)
- `Alt+Up` / `Alt+Down` — Transpose selected MIDI note group. (`src/input/midi_editor_input_commands.c`, `midi_editor_handle_note_command_keydown`)

### Track name editor (timeline header)
- `Enter` — Commit rename. (`src/input/timeline/timeline_input_keyboard.c`, `timeline_input_keyboard_handle_event`)
- `Esc` — Cancel rename. (`src/input/timeline/timeline_input_keyboard.c`, `timeline_input_keyboard_handle_event`)
- `Backspace` — Delete previous character. (`src/input/timeline/timeline_input_keyboard.c`, `timeline_input_keyboard_handle_event`)
- `Delete` — Delete next character. (`src/input/timeline/timeline_input_keyboard.c`, `timeline_input_keyboard_handle_event`)
- `Left` / `Right` — Move cursor. (`src/input/timeline/timeline_input_keyboard.c`, `timeline_input_keyboard_handle_event`)

### Tempo overlay (timeline mode)
- `Up` / `Down` — Adjust selected tempo event BPM. (`src/input/timeline/timeline_input_keyboard.c`, `timeline_input_keyboard_handle_event`)
- `Shift+Up` / `Shift+Down` — Adjust BPM in larger steps. (`src/input/timeline/timeline_input_keyboard.c`, `timeline_input_keyboard_handle_event`)
- `Delete` / `Backspace` — Remove selected tempo event. (`src/input/timeline/timeline_input_keyboard.c`, `timeline_input_keyboard_handle_event`)

### Automation edit (timeline mode)
- `Delete` / `Backspace` — Remove selected automation point. (`src/input/timeline/timeline_input_keyboard.c`, `timeline_input_keyboard_handle_event`)

## Transport / Tempo UI
### Tempo text edit (BPM or time signature)
- Type to insert, `Backspace` to delete, `Left` / `Right` to move cursor. (`src/input/transport_input.c`, `transport_input_handle_event`)
- `Enter` — Commit edit. (`src/input/transport_input.c`, `transport_input_handle_event`)
- `Esc` — Cancel edit. (`src/input/transport_input.c`, `transport_input_handle_event`)

### Tempo focus (not editing text)
- `Up` / `Down` — Adjust BPM or time signature at playhead. (`src/input/transport_input.c`, `transport_input_handle_event`)
- `Shift+Up` / `Shift+Down` — Larger BPM steps. (`src/input/transport_input.c`, `transport_input_handle_event`)

## Inspector (clip inspector)
### Clip name edit
- `Enter` — Commit rename. (`src/input/inspector_input.c`, `inspector_input_handle_event`)
- `Esc` — Cancel rename. (`src/input/inspector_input.c`, `inspector_input_handle_event`)
- `Backspace` / `Delete` — Delete character. (`src/input/inspector_input.c`, `inspector_input_handle_event`)
- `Left` / `Right` — Move cursor. (`src/input/inspector_input.c`, `inspector_input_handle_event`)

### Numeric field edit (timeline start/end, source, playback rate)
- `Enter` — Commit edit. (`src/input/inspector_input.c`, `inspector_input_handle_event`)
- `Esc` — Cancel edit. (`src/input/inspector_input.c`, `inspector_input_handle_event`)
- `Backspace` / `Delete` — Remove last character. (`src/input/inspector_input.c`, `inspector_input_handle_event`)

### Inspector focus (no text edit)
- `Up` / `Down` — Nudge clip gain. (`src/input/inspector_input.c`, `inspector_input_handle_event`)

### Fade curve selection
- `Left` / `Right` — Nudge selected fade curve. (`src/input/inspector_fade_input.c`, `inspector_fade_input_handle_keydown`)

## Effects panel
- `Left` / `Right` / `Up` / `Down` — Reorder selected effect slot when the panel is focused. (`src/input/effects_panel_input.c`, `effects_panel_input_handle_event`)
- `Shift+Arrow` — Move selected slot to start/end of chain. (`src/input/effects_panel_input.c`, `effects_panel_input_handle_event`)

## Library browser (rename mode)
- Mouse:
  - Top-right header buttons toggle library mode: `SOURCE` vs `IN PROJECT`. (`src/ui/library_browser.c`, `library_browser_render`; `src/input/library_input.c`, `library_input_handle_primary_click`)
  - Drag/drop `.wav` or `.mp3` files from Finder onto the library pane to import (copy) into DAW library copy root with collision-safe naming. (`src/input/input_manager.c`, `input_manager_handle_event`; `src/input/library_input.c`, `library_input_handle_drop_file`)
- `Left` / `Right` — Move cursor. (`src/input/library_input.c`, `library_input_handle_event`)
- `Backspace` / `Delete` — Delete character. (`src/input/library_input.c`, `library_input_handle_event`)
- `Enter` — Commit rename on disk. (`src/input/library_input.c`, `library_input_handle_event`)
- `Esc` — Cancel rename. (`src/input/library_input.c`, `library_input_handle_event`)

## Project modals
### Save prompt
- Type to insert; `Backspace` to delete; `Left` / `Right` to move cursor. (`src/input/input_manager.c`, `project_prompt_handle_event`)
- `Enter` — Save project; closes prompt. (`src/input/input_manager.c`, `project_prompt_handle_event`)
- `Esc` — Cancel prompt. (`src/input/input_manager.c`, `project_prompt_handle_event`)

### Load modal
- `Enter` — Load selected project. (`src/input/input_manager.c`, `project_load_handle_event`)
- `Esc` — Close modal. (`src/input/input_manager.c`, `project_load_handle_event`)
