# Timeline navigation

Main Edit shares one lane geometry between its painter, pointer owners, selection
reveal and scrolling bounds. All tracks remain reachable when their combined row
height exceeds the timeline pane. The toolbar and loop ruler stay fixed; lane
content and track controls are clipped beneath them.

- Wheel or trackpad Y movement scrolls the track list vertically. Fractional
  trackpad deltas are retained; flipped-device direction is respected.
- Trackpad X movement or Shift-wheel scrolls time horizontally. Shift-wheel now
  navigates the time window rather than moving the playhead.
- Ctrl-wheel changes track height around the fractional row under the pointer.
- The transport Track Size slider and Height buttons preserve the center row;
  Fit Height uses the actual lane viewport and returns to the first track.
- Alt-wheel changes the time scale around the time under the pointer.
- `Height -` and `Height +` visibly decrease/increase row height. These controls
  use the normal matching press/release ownership, wrap with the other toolbar
  controls and disable at the supported height limits.
- The right-side scrollbar represents the whole track list. Drag its thumb or
  click the trough to page. A captured thumb remains bounded beyond the pane and
  cancels on focus/window transitions.

Selecting a row reveals it with the smallest necessary movement. A selected row
that was fully visible stays visible through window/panel resizing; browsing away
from the selection does not force it back into view. Scroll bounds recompute from
live row count and viewport height. Removing tracks clamps obsolete offsets.
Toolbar pixels, row spacing and out-of-pane pixels do not resolve as tracks.

Vertical offset is transient view state. Project loading resets it; existing
project row-height persistence remains supported. This change does not alter the
session schema, musical data, MIDI editor click isolation, instrument knobs,
audio routing or export behavior.

`make test-timeline-navigation` exercises 32 tracks at minimum/default/maximum
heights, three window sizes, panel changes, first/last/all-row reachability,
fractional/two-axis/flipped wheel movement, anchored zoom, matching button release,
scrollbar capture, selection and shrinking track-count bounds. Passing a capture
prefix to the built test runs three real Vulkan product-painter frames for top,
bottom and resized bottom layouts, with validation enabled. Use the repository's
installed validation-layer environment as in `tools/verify-vulkan-rollout.py`.
Automated geometry/input and native rendering proof remain separate from human
trackpad/listening acceptance.

Nested bus groups and collection-scoped Load/Save are implemented in subsequent slices;
this navigation change does not implement those surfaces.
