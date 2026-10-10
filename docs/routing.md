# Guarded shared returns

The development engine, project format, common application actions and local MCP adapter support post-fader source-to-return routing. The native Effects panel exposes Routing for guarded return creation and staged send/role editing. Return lanes use the existing Effects panel for their inserts.

## Agent workflow

1. `get_routing` returns every existing track's stable target, return role and four send slots, with an exact `expected_routing` token. Reads create no files and consume no mutation receipts.
2. `create_return` requires endpoint instance, unique request ID and the current routing token. It appends one empty return in one publication and one shared undo step. It works from zero tracks and returns `created_target`. No separately visible ordinary-lane intermediate is published.
3. Use existing `get_effect_chain` / `edit_effect_chain` or effect-preset recall to configure the return's processors. Return processing includes its inserts, dedicated EQ, fader and pan before the master.
4. `edit_routing` accepts one through sixteen edits in one guarded batch. A `send` edit contains source `target`, zero-based `slot` (0..3), `return_target` and linear `gain` (0..1). Use explicit null destination and zero gain to disconnect. A `role` edit contains an existing track target and Boolean `is_return`; converting a source to a return requires no regions or enabled instrument. The complete final graph is validated once, permitting coordinated disconnection/role changes.
5. `undo_routing` / `redo_routing` use the current token and consume only a matching routing command at the shared history cursor. Creation redo preserves the accepted return identity. Creation undo refuses removal if later unrecorded lane names, mixer/EQ/instrument settings, automation, regions or effects would be discarded. Other commands require their own history operations.

There are at most four returns, four send slots per source and 128 tracks on this routing surface. Missing/duplicate destinations, feedback, return-to-return sends, repeated edits of the same slot/role, unknown fields and invalid levels refuse the whole batch. Source sends occur after source inserts/EQ, fader, pan and source-stage compensation. Each return processes the aggregated input once. Source solo retains its returns; return solo suppresses dry source output while retaining contributing inputs.

## Native workflow

Open **Routing** in the Effects header. Previous/Next selects an existing lane; New Return appends one empty return through the same common action as MCP, including from an empty project. Click a source send destination to cycle through Off and existing returns, enter its level as a percentage from 0 to 100, and press Apply or Enter. Destination, level and role fields remain staged until Apply validates and publishes the complete graph as one undo step. Off requires zero level. Return lanes receive source sends; close Routing to edit their effects.

Refresh explicitly captures external routing changes; stale Apply refuses while preserving typed input. Role conversion requires an empty lane without an enabled instrument and cannot discard incoming bindings. Focus loss, project replacement and teardown release the form and shared text ownership. Buttons require matching pointer releases. Authoring requires a pane at least 360 by 275; compact panes show an enlargement message and disable authoring.

## Guards and recovery

Four complete routing captures are retained per endpoint with exact-value token reuse. The guard covers project generation, ordered track identities, return roles and complete destination/level fields. It excludes mixer, effects, names and region content because ordinary routing restoration does not replace those settings; role conversion and creation undo separately validate the content they would affect. A changed topology, routing or project lifetime refuses stale tokens. An identical no-op creates no history.

Writes refuse conflicting playback, recording, Test, bounce and active authoring gestures. They use the existing opt-in same-user write endpoint and immutable 128-receipt ledger. After a lost response, recover with `request_status` using the same instance/request ID. Exact replay returns its historical result; use a fresh read to inspect current state. Client restart/configuration is required to discover the five additive tools, bringing the local server to 45 tools. Building source does not register this server in an existing chat.

## Persistence and rendering

Projects embed complete routing and return processor settings. Stored destinations are one-based references into the complete owned saved track array; zero means disconnected. Reopening resolves those references to fresh runtime identities after every lane exists. Missing legacy fields default to ordinary tracks and disconnected sends. Invalid routing or failed private candidate preparation preserves the current project.

Live/private offline plans own their return buffers and destination maps. Separate source and return compensation groups align dry and processed paths before master processing. Project/source dry capture omits return duplication; an isolated dry return exposes its input sum. Production composition analysis and captured WAV publication use the same routing path. Stopped instrument audition retains the existing two-second effects-tail policy.

## Verification boundary

`test-routing-actions` covers empty creation, one publication/history, stable redo identities, later-edit protection, return capacity, owner/busy/stale/no-op/refusal behavior and exact production audio after undo/redo. `test-engine-routing` checks independent parallel-return signal factors, solo/mute/fader/isolation/dry behavior and actual delayed-signal compensation. Session transaction/serialization tests cover saved routing and exact processed audio reopening. `test-mcp-routing` uses the official SDK and production bridge for creation, send edits/history, token eviction, receipts, malformed graphs and read-only admission. ASan and the broad stable suite are required at the corresponding development checkpoints.

Actual native App_Run acceptance also creates a return and connects its send through MCP, configures and undoes return effects, verifies independent dry/wet ratios, then performs normal save and real restart with exact routing/metrics/plots/WAV parity. Source music is fixture-authored. Native routing authoring is covered by the focused form test and actual native input/painter harness. Listening acceptance and installed/global client adoption remain separate. Bus-to-bus routing, pre-fader sends and external sidechain source selection are not exposed by this initial routing graph.

`test-routing-ui` covers native creation from both populated and empty projects, shared percentage text, unpublished staging, one-step application, stale/busy/invalid/duplicate-destination refusal, mismatched releases, compact forms and focus cleanup. The native Vulkan harness opens Routing through the Effects header, creates a return, types and applies a 50% source send, and paints source/return views with enclosing clip preservation and clean validation.
