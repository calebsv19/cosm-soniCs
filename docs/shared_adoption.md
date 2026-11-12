# Shared inputs and local build identity

The ordinary consumer selects `third_party/codework_shared` by default. The local
manifest `config/shared_adoption.json` binds 410 files across 26 modules to the
committed soniCs vendor snapshot at `6071a45`. It preserves existing module
versions and intentional imported history; it does not upgrade to shared HEAD.
Twenty-five archives and direct timer HUD/platform sources are separate inputs.

Every ordinary app build validates selected module bytes, including when Make
could otherwise reuse output. Missing, changed, symlinked or newly introduced
source inputs fail admission. Alternate roots must match the bound module bytes.
The compiler, flags, link flags, roots, Make rules and adapters form an identity
stamp. Stable identity retains its timestamp. Shared archive recipes no longer
run recursive clean. They reconstruct the selected archive and force module
compilation only when consumer prerequisites require it; unrelated output stays.

The host compiler records actual transitive headers for each shared compilation.
Generated dependency rules use the consumer archive target spelling. Conservative
module source/header dependencies also cover nested module builds. App and host
objects retain their native Make dependency files. This local Clang route does
not establish dependency correctness for fisiCs, which has different dependency
emission, or qualify other platforms and release-contained producer routes.

Run `python3 tests/test_shared_build_identity.py --disposable-root <copy>` only
against a disposable non-Git copy under `/private/tmp`; it mutates fixtures and
checks no-op reuse, header/rule/compiler/flag invalidation and pin refusal/retry.
No shared source, installed application or release state is changed by adoption
of this consumer policy. Package identity and runtime acceptance are separate gates.
