# Bootstrapping BaristaScript from Foundry

How to get BaristaScript from an empty GDExtension scaffold to a working language in the
shortest credible path, by reusing what already exists in the Foundry tree.

Companion documents:

- [`GRAMMAR.md`](GRAMMAR.md) — the language BaristaScript is specified to be, and its §0.2 delta
  table against Foundry Script.
- [`namespace-engine-support.md`](namespace-engine-support.md) — what stock Godot 4.7.2 does with a
  namespaced global class, and why no engine fork is needed.

The recognition-only scaffold milestone this plan builds on rather than replaces is described in §7
under M0.

---

## 1. The headline

Foundry's `modules/foundry_script/` is ~152,000 lines of C++ plus a **3,362-file golden test
corpus**. The reusable fraction is much higher than a rewrite instinct suggests, because the
expensive part of a language is its front end, and a front end is pure compile-time code that
touches almost nothing engine-specific.

| Layer | LOC | Verdict |
|---|---:|---|
| Tokenizer | 2,895 | **Port near-verbatim** |
| Parser | 13,570 | **Port near-verbatim** |
| Analyzer | 28,336 | **Port, with edits at the delta sites** |
| Type model | 3,037 | Port, with edits |
| Bytecode codegen | 3,892 | Port, plus monomorphization |
| Bytecode format / loader / verifier | 5,594 | Port near-verbatim |
| VM + callables | 11,507 | Port, rebound to godot-cpp `Variant` |
| Compiler driver + cache | 8,659 | Port, with edits |
| Formatter | 4,218 | **Port verbatim** |
| Linter + warnings | 1,082 | **Port verbatim** |
| Name mangler | 7,938 | Port verbatim (export obfuscation) |
| Numeric ops | 953 | **Delete** — BaristaScript has one integer type (D1) |
| Conformance registry | 1,939 | Port verbatim |
| Autoload / project index | 1,632 | Port, plus the D8 build step |
| Build pipeline | 2,660 | **Drop** — lives in Foundry's engine core |
| Language server | 9,774 | Port; mostly standalone JSON-RPC |
| Engine glue | 8,859 | **Rewrite** against `ScriptLanguageExtension` |
| Editor integration | 23,963 | **Mostly rewrite**; salvage the highlighter and docgen |
| TextMate grammar generator | 1,202 | **Reuse as data** |
| Test corpus | 3,362 `.barista` files | **Reuse as the conformance suite** |

Roughly **65% ports with light edits, 10% needs real rework, 20% is rewritten, 5% is deleted.**

## 2. The single biggest win: the test corpus

`modules/foundry_script/tests/scripts/` is a golden-file corpus organized by pipeline stage. Each
case is a `.fs` source file paired with a `.out` file holding expected output (`FS_TEST_OK`, or the
exact diagnostic text).

| Directory | Files | Reuse |
|---|---:|---|
| `analyzer/errors` | 971 | High — most diagnostics are unchanged |
| `runtime/features` | 557 | High |
| `analyzer/features` | 529 | High |
| `runtime/errors` | 259 | High |
| `parser/errors` | 182 | **Verbatim** — the parser is unchanged |
| `parser/features` | 133 | **Verbatim** (135 `.fs`, two of them `.notest.fs` helpers) |
| `analyzer/warnings` | 71 | High |
| `refactor` | 60 | Medium — depends on editor integration |
| `lsp` | 44 | Medium |
| `parser/warnings` | 29 | **Verbatim** |
| `completion/*` | ~150 | Medium |

This corpus is worth more than any single source file in the tree. It converts "did the port work?"
from a judgement call into a number, and it is the mechanism that makes the piecemeal strategy
safe: each ported stage is done when its slice of the corpus is green.

**Migration is mostly mechanical.** Rename `.fs` → `.barista`, rename `.out` expectations from
`FS_TEST_OK` to `BS_TEST_OK`, and triage the cases that touch a delta. Expect the following to need
hand review, identified by grepping the corpus:

- **Delete outright (D1):** any case using `uint`, `ulong`, `long`, a `U`/`L`/`UL` literal suffix,
  or `as!`. These are not triaged case by case — the feature is gone, so the tests go with it. That
  is **113 corpus cases**, identified by one grep.
- Rewrite `is long` to `is int` wherever a case tests the integer carrier (D1).
- cases constructing a generic without type arguments, or relying on the gradual store rule (D3, D4)
- cases asserting engine-visible traits or `trait_name`/`enum_name` global registration (D5, D6)
- cases using namespaced native classes (D7)

Foundry's own test file names make this triage cheap: `test_integer_literal_suffixes.h`,
`test_integer_literal_suffixes.h`, `test_integer_promotion.h`, `test_native_integer_metadata.h`,
`test_checked_numeric_runtime.h`, `test_numeric_ops.h`, `test_typed_container_numeric_width.h`,
`test_typed_store_width.h`, and `test_call_argument_conversion_width.h` are the numeric-tower
cluster — **3,720 lines of C++ suites that are simply not ported**. `test_generic_runtime.h`,
`test_generic_analyzer.h`, `test_projected_type_evidence.h`, and `test_self_contract_gradual_values.h`
are the generics cluster; leave those for the milestone that owns them.

Also reusable: `tests/fs_test_runner.{h,cpp}`, `tests/fs_fixture_cli.{h,cpp}`, and
`tests/scripts/`'s directory convention. The runner itself is Foundry-internal (it uses doctest
inside the engine's test harness), so the harness is rewritten while the *corpus format* is kept.

## 3. The porting seam

The frontend's dependency on the engine is almost entirely on containers and strings, not on
engine behavior. Measured across the module, the most-used core headers are:

```
74  core/io/file_access.h        30  core/variant/variant.h
46  core/config/project_settings.h  30  core/templates/hash_set.h
41  core/io/dir_access.h         29  core/object/class_db.h
37  core/templates/vector.h      21  core/variant/container_type_validate.h
37  core/string/ustring.h        20  core/templates/hash_map.h
36  core/object/script_language.h
```

**Strategy: one compatibility header, `bs_platform.h`,** that maps every `core/*` type the frontend
uses onto its godot-cpp counterpart, so ported files change their includes and nothing else.

**Already confirmed against the scaffold.** `src/barista_script.h` compiles-by-declaration against
godot-cpp 4.7 with `_get_documentation`, `_get_class_icon_path`, `_get_script_method_argument_count`,
`_get_method_info`, `_get_script_signal_list`, `_get_script_property_list`, `_get_constants`,
`_get_members`, `_get_rpc_config` and **`_is_abstract`** all present as overrides. So documentation,
icons, signals, exported properties, RPC config and the `abstract` modifier need no workaround — none
of them appears in the GRAMMAR §0.2 delta table. Equally confirmed by its *absence*: there is no
`_get_script_trait_list`, which is D5.

> **Unverified assumption — resolve this first.** godot-cpp is believed to mirror most of
> `core/templates/*` (`Vector`, `HashMap`, `HashSet`, `List`, `LocalVector`, `RBMap`) and
> `String`/`StringName`/`Variant`. The `godot-cpp` submodule in this repository is currently empty,
> so that was **not** confirmed. **Milestone 1 task zero** is to populate the submodule and diff its
> header set against the list above. If a container is missing, vendor it from Godot's MIT-licensed
> core rather than rewriting call sites — the containers are self-contained and this keeps the
> frontend diffable against Foundry.

**The global class registry maps cleanly.** `ScriptServer`'s `global_classes` is keyed on
`StringName`, so a namespaced class registers under its qualified name with no engine change, and
`ScriptLanguageExtension::_get_global_class_name` lets the extension supply it. Namespaces cost the
seam nothing. The investigation is in
[`namespace-engine-support.md`](namespace-engine-support.md); §5.6 above is the one place the
registry's fixed shape does constrain the design.

Known non-mappings, which `bs_platform.h` cannot paper over and which need real work:

| Foundry dependency | Why it does not map | Action |
|---|---|---|
| `core/variant/container_type_validate.h` | Foundry extended it with `numeric_type` and `type_arguments` | Own type-descriptor struct; D2 |
| `core/variant/numeric_type.h` | Foundry's own addition | **Not needed** — deleted with the tower (D1) |
| `core/object/script_language.h` (trait virtuals) | Not in stock | Own trait registry; D5 |
| `core/object/script_function_state.h` | Engine-internal | Own coroutine object (GRAMMAR §10) |
| `core/config/project_build_pipeline_*` | Foundry engine core | Drop |
| `core/object/script_diagnostic_capture.h` | Foundry engine core | Own diagnostic sink |
| `ERR_FAIL_*` macros | godot-cpp spells them differently | Thin macro shim |

## 4. What ports verbatim, and why

**Tokenizer (2,895).** Nothing in §2 of the grammar changed except the file extension and the
`ulong` literal bound. Depends on `String`/`char32_t` only.

**Parser (13,570).** The entire §5 precedence table, every declaration form, `match` patterns, the
type grammar — all identical. The parser produces an AST; it decides nothing about representation.
This is the highest-value verbatim port in the tree.

**Formatter (4,218) and linter (1,082).** Pure AST-to-text and AST-to-diagnostic. No engine contact.

**Name mangler (7,938).** Operates on the declaration index for export obfuscation. `KEEP_RULES.md`
comes with it.

**Conformance registry (1,939).** Already a standalone registry keyed on `(target, trait)` — exactly
what GRAMMAR §10 says BaristaScript must own for itself. It was never using engine facilities.

**Bytecode format, loader, verifier (5,594).** Self-contained serialization. The `.fsb`/`.fsc`
container becomes `.bsb`/`.bsc`.

## 5. What needs real work

### 5.1 Numerics (D1) — delete, do not port

BaristaScript has one integer type, `int`, on the signed 64-bit carrier — identical to GDScript's.
`fs_numeric_ops` (953 lines), `NumericType`, the literal suffixes, `as!`, and the whole
signedness/width apparatus threaded through 28 source files are **not ported**.

This was the most expensive item in the plan and it is now the cheapest. The justification is in
GRAMMAR §0.4: across 720 hand-written `.fs` files in FoundryLib, FoundryKit, Foundry-Tools and
Foundry-Examples, `uint` and `ulong` appear in **zero** of them, and `protoc-gen-foundryscript`
already maps every protobuf integer — `uint64` and `fixed64` included — to plain `int`. The tower is
unused where it is fully powered, so shipping a weakened version of it here was never going to earn
its cost.

The only work is the ~20 lines that make the removed spellings **reserved** rather than free
identifiers, so ported Foundry Script fails loudly (GRAMMAR §2.5, §2.6.1, §2.8).

### 5.2 Monomorphization (D2–D4) — new pass in codegen

Foundry reifies type arguments on the instance. BaristaScript emits one raw base script plus one
specialization script per distinct argument vector (GRAMMAR §4.3.1). This is new code, not a port,
and it sits between the analyzer and `fs_byte_codegen`.

The compensation: because a specialization is a distinct script, the whole of Foundry's generic
`is`/`as` semantics is recovered for free, so `test_generic_runtime.h`'s expectations largely hold.
What is *deleted* rather than ported is Foundry's gradual-store machinery — the "specialized types
at a store boundary" and per-component knownness rules — which D3 makes unnecessary. That is a
simplification worth several hundred lines of analyzer.

### 5.3 Engine glue (8,859) — rewrite

`foundry_script.cpp` implements `ScriptLanguage`/`Script` against the engine's internal C++
interface. The BaristaScript equivalent implements `ScriptLanguageExtension`/`ScriptExtension`/
`ScriptInstanceExtension` against godot-cpp's virtual dispatch. Same responsibilities, different
shape, so this is a rewrite that *reads* the Foundry file rather than porting it.

The scaffold design already stubs the three classes. This milestone fills them in.

### 5.4 Coroutines — new

`await` cannot use `ScriptFunctionState`. The VM needs its own suspended-frame object resumed by a
signal connection. `fs_vm.cpp`'s suspension logic ports; its engine handoff does not.

### 5.5 Editor integration (23,963) — mostly rewrite, some salvage

Worth salvaging: `fs_highlighter.{cpp,h}` (a `SyntaxHighlighter` subclass, which godot-cpp exposes),
`fs_docgen.{cpp,h}` (feeds `_get_documentation`), and `fs_editor_export_plugin.{cpp,h}` (maps onto
`EditorExportPlugin`).

Not portable: `fs_migration_wizard*`, `fs_build_pipeline_settings`, `fs_refactoring_*`, and
`fs_project_scan` all reach into editor internals a GDExtension does not get. Treat editor
integration as the last milestone and the one most likely to be reduced in scope.

### 5.6 The global-class export index (D5, D6) — new, and load-bearing at export time

Foundry teaches `ScriptServer::add_global_class` two extra flags, `is_trait` and `is_enum`, and
persists them through `global_script_class_cache.cfg` in `script_class_save_global_classes()`. A
GDExtension cannot do that: stock's cache writer emits a **fixed** set of keys
(`class`, `language`, `path`, `base`, `icon`, `is_abstract`, `is_tool`), and there is no hook to add
to it.

In the editor this costs nothing — BaristaScript re-derives the kind of each declaration from its own
parse on every scan. **In an exported project there is no scan.** The cache is the only surviving
record of the project's global classes, and it cannot carry "this one is an enum," "this one is a
trait," or any of the cross-file declaration data (retroactive conformances, the D8 autoload index)
that Foundry keeps alongside it.

So BaristaScript needs its **own** index resource, built at export and shipped in the PCK, rather
than leaning on Godot's class cache. Foundry hit the same wall from its privileged side and still
needed a parallel mechanism — its cache writer carries a note that declaration-only files "export no
global class, so they need their own entry in the same cache."

This is a **requirement on M3/M4**, not a late polish item: the analyzer needs somewhere to write the
index, and the runtime needs to load it before the first script resolves a global name. Discovering
it at export time means retrofitting a serialization format into a finished compiler.

What does *not* need to be solved: keeping non-instantiable declarations out of the editor. That
falls out of reporting an empty base and `_is_abstract() = true`, with no registry flags and no engine
patch — see [`namespace-engine-support.md`](namespace-engine-support.md) §5.

## 6. Reuse from the wider ecosystem

| Source | What | Effort |
|---|---|---|
| `Foundry/modules/foundry_script/grammar/` | TextMate grammar generator (`tmlanguage_builder.py`) + `patterns/*.json` | **Data reuse** — retarget scopes to `source.barista` |
| `CafecitoGames/FoundryScript` | Shipped VSCode extension: `foundry-grammar.json`, `language-configuration.json`, packaging | **Fork and rename** — day-one editor support outside Godot |
| `CafecitoGames/Foundry-Tools` | `protoc-gen-foundryscript` (Go) and `anvil` package manager | **Fork** — codegen retargets to `.barista`; needs the D2 `uint64` decision from GRAMMAR §7.1 |
| `CafecitoGames/FoundryLib` | `foundry.testlib`, `foundry.logging` — pure `.fs` libraries | **Translate** — mechanical, and they exercise the language end-to-end |
| `Foundry/tools/foundry-test-adapter` | Python TAP13 test adapter, ~2,400 lines with fixtures | **Reuse** — engine-independent |
| `CafecitoGames/Foundry-Script-Intelligence` | Python training/intelligence pipeline | Evaluate later; not on the critical path |

## 7. Milestones

Sequenced so that every milestone ends with something runnable, and so the corpus proves each one.

**M0 — Recognition (in progress).** Per the scaffold design: `.barista` files load as
non-executable `Script` resources. Already on disk: the template layout, `SConstruct`/`CMakeLists`,
`project/bin/barista_script.gdextension` (`compatibility_minimum = 4.7`), `project/example.barista`,
a smoke test, and `src/barista_script.{cpp,h}` declaring `BaristaScript : ScriptExtension` with its
stub virtuals. Outstanding: the language and resource-loader classes, and initializing the
`godot-cpp` submodule. *Exit: a fixture file appears in the editor.*

**M1 — Platform seam.** Populate `godot-cpp`, diff its headers against §3, write `bs_platform.h`,
vendor any missing container. *Exit: an empty translation unit including `bs_platform.h` compiles in
the extension.* This is the milestone that de-risks everything after it.

**M2 — Tokenizer + parser.** Port both verbatim through the seam. Bring over
`tests/scripts/parser/**`, which is **344** cases and not the 345 this plan first quoted: 133
`features` + 182 `errors` + 29 `warnings`, matching the 344 `.out` files exactly. The two remaining
`.fs` in `features/` are `.notest.fs` helper sources with no `.out`, not cases. Sixteen of the 344
are triaged for D1, four of them by deletion. *Exit: 340/340 parser cases green* (issue #10).

**M3 — Type model + analyzer.** Port the type model and analyzer, dropping the numeric apparatus
(D1) rather than porting it. Bring over `analyzer/**` (1,571 cases) minus the 113 deleted numeric
cases and the generics cluster. Define the global-class index format here (§5.6) — the analyzer is
what fills it. *Exit: the non-generic analyzer corpus green.*

**M4 — Codegen + VM + glue.** Port bytecode and VM, rewrite the glue, implement coroutines. This is
the first milestone where a `.barista` file *runs*. Load the §5.6 index at startup, since Godot's
class cache cannot carry it into an exported project. Bring over `runtime/**` (816 cases). *Exit: a
`.barista` script drives a Node in a stock Godot scene.*

**M5 — Monomorphization.** Add the specialization pass, re-enable the generics cluster. *Exit:
generic corpus green under D2–D4.*

**M6 — Tooling.** Formatter, linter, LSP, TextMate grammar, VSCode extension.

**M7 — Editor integration.** Highlighter, docgen, export plugin, the `@autoload` build step (D8).

**M8 — Ecosystem.** `protoc-gen-baristascript`, package manager, testlib translation.

M2–M3 carry the most reused code and the least new design. M5 is the riskiest, which is why it is
deliberately sequenced *after* the language already runs. Dropping the numeric tower removed a whole
milestone: the plan was ten and is now nine.

## 8. Licensing

Foundry is a fork of Godot and carries Godot's MIT licence; `modules/foundry_script/` is a
derivative of `modules/gdscript/`. BaristaScript is a further derivative of both. Ship
`GODOT_LICENSE.txt` alongside BaristaScript's own licence and state the derivation in the README,
the way Ruzta ships `GDSCRIPT_LICENCE.txt`. Since all three repositories are yours, the only
obligation is attribution.

## 9. Open questions

1. **Godot version.** The scaffold has already committed to 4.7 (`compatibility_minimum = 4.7` in
   `barista_script.gdextension`, godot-cpp `10.0.0-rc2`). The Uzir client is on 4.6. If Uzir is the
   first consumer, either Uzir moves to 4.7 or the extension is retargeted to 4.6 — and RC2 was
   chosen precisely because it is the newest tagged binding exposing 4.7. Confirm before M1, because
   it fixes the API the seam is written against, and retargeting later invalidates `bs_platform.h`.
2. **Wide `uint64` protobuf fields.** With one `int` (D1), a protobuf `uint64` at or above `2^63`
   round-trips its bit pattern but reads as negative. Uzir's protocol has 50 `uint64` fields; today
   they already generate as `int` under `protoc-gen-foundryscript`, so this is not a regression, but
   it is worth confirming none of them is a hash or a random 64-bit id rather than a counter,
   timestamp, or sequential identifier. If any is, `protoc-gen-baristascript` should emit an
   accessor pair for that field rather than exposing the raw value.
3. **Shared source or hard fork.** This plan assumes BaristaScript is a **fork** of the Foundry
   frontend, diverging from day one. The alternative — one source tree compiled two ways behind
   `#ifdef BS_STOCK_ABI` — avoids fixing every parser bug twice while both languages are alive. The
   fork is simpler and matches the piecemeal strategy; the shared tree is cheaper if Foundry keeps
   moving at its current rate. Worth an explicit decision before M2, since that is where the
   duplication starts.
