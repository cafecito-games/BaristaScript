# Debug-only parse-cache binding design

## Context

Issue #54 identifies `BaristaScriptParseCache` as an engine-facing test adapter that is registered in release builds even though production code uses `BSCache` directly. Its ClassDB surface includes source overrides, parser-cache mutation, dependency manipulation, and fault-injecting persistence methods. The adapter's current GDScript consumers are all debug suites.

## Chosen design

Compile and register the complete `BaristaScriptParseCache` adapter only when `DEBUG_ENABLED` is defined. Keep `BSCache`, `BSParserRef`, and their production lifecycle unchanged and available in release builds. This treats the adapter as a debug probe instead of maintaining a partial allowlist whose safety would have to be re-audited whenever a new forwarding method is added.

The implementation will place the class declaration and implementation behind the same build guard and move its registration beside the other debug-only probes. Its include in `register_types.cpp` will also be conditional so release translation units do not refer to the adapter.

## Alternatives considered

1. Gate only source-override and fault-injection methods. This minimizes API churn but leaves raw parser and dependency mutation reachable and makes future forwarding methods release-visible by default.
2. Retain read-only inspection methods while gating every mutator. This is safer than a narrow blocklist but still creates an allowlist boundary with no production consumer.
3. Document the full adapter as trusted-only. This preserves compatibility but does not reduce the release attack surface.

The complete debug-only adapter is the smallest policy surface and directly matches how the class is used.

## Verification

Add a repository check that examines built extension artifacts for the unique `BaristaScriptParseCache` marker. Debug artifacts must contain the marker; release artifacts must not. Run the check for every CI matrix build after compilation so Windows and Web release artifacts are covered, and use the same check against local debug and release builds.

Run the existing debug GDScript suites to prove source overrides, fault injection, and analyzer cache controls remain available. Run both SCons targets, the CMake debug build, CI validation, license-header validation, and clang-format validation before review.

## Scope boundaries

Do not change the on-disk cache format, internal production parser cache behavior, `BSParserRef` registration, or declaration-index probe. No compatibility promise exists for the test-only ClassDB adapter in release templates.
