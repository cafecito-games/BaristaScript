# The BaristaScript runtime

How a `.barista` file gets from source to a running script instance, and which file owns which
part of that path. This document is seeded by the vertical slice and completed by the milestone's
documentation child; what is written here is what the slice froze.

## The pipeline

```
BSCache                     parse + analyze (already existed)
   |  BSParser tree + analyzer DataTypes + declaration index + conformance registry
   v
BSCompiler                  src/bs_compiler*.cpp
   |  walks the analyzed tree and emits through ...
   v
BSCodeGenerator             src/bs_codegen.h -- the abstract emitter interface
   +- BSByteCodeGenerator   src/bs_byte_codegen.* -- fills the function tables, in memory only
   |
   v
BSFunction                  src/bs_function.h -- code, constants, global names, method and lambda
   |                        tables; addresses are a 24-bit index plus a stack/constant/member tag
   v
BSFunction::call            src/bs_vm.cpp plus the per-family src/bs_vm_ops_*.inc handlers
   |
   +- BSInstance            src/bs_script_instance.* -- a plain struct plus the one static
   |                        GDExtensionScriptInstanceInfo3 vtable in the extension
   v
BaristaScript               the script-level surface: compiled members, methods, base type,
                            instantiation and reload
```

There is no on-disk form. A compiled function lives as long as the script that owns it and is
rebuilt from source on reload; an exported project ships `.barista` source, the way Godot ships
GDScript source.

## The opcode enum is frozen

`src/bs_function.h` owns the opcode list, the address encoding and the function tables.
`src/bs_codegen.h` owns the emitter interface. Both are frozen: **an opcode keeps its number.** A
compiled function built by one translation unit has to be read the same way by every other, and a
renumbering breaks that silently.

**A new opcode is inserted immediately before `END`, never after it.** Every emitted function ends
with `END`, and `END`'s own number is the one number nothing depends on: it is written by exactly one
line of the emitter and read by exactly one handler, so moving it costs nothing. Keeping it last is
what keeps "the terminator is the last enumerator" true — the name table's `static_assert`, the
committed case that asserts `OPCODE_END == OPCODE_MAX - 1`, and anyone reading the list to see where
it ends. Dispatch itself would run an opcode numbered after `END` perfectly well; the invariant is
about the list, not the loop.

The list holds **171 opcodes** plus the `OPCODE_END` terminator, derived from Foundry's 180 at
`c9d5e35` by removing two families:

| Removed | Opcodes | Why |
| --- | --- | --- |
| Numeric (D1) | `NUMERIC_BINARY`, `NUMERIC_UNARY`, `NUMERIC_CAST`, `NUMERIC_REINTERPRET`, `TYPE_ADJUST_UINT` | The width/signedness tower is deleted, not deferred: a slot's Variant carrier is its whole numeric type. |
| Specialization | `GET_TYPE_PARAMETER`, `ASSIGN_TYPED_CLASS_PARAMETER`, `CONSTRUCT_SPECIALIZED`, `MAKE_SPECIALIZED_CLASS_HANDLE` | None of them can be given a meaning before a receiver carries reified type arguments. Generics appends them. |

Fourteen of the 171 are **reserved and never emitted**: the ten `*_VALIDATED` opcodes and the four
validated method-bind and native-static calls. Every call goes through the generic Variant paths,
and the reserved slots are what makes a later validated fast path a local change.

**An opcode with no handler fails closed.** The dispatch loop's default arm raises
`Opcode not implemented: <NAME>.` on the engine's script-error channel and abandons the frame.
There is no silent fall-through and no undefined behaviour, and the name comes from the same list
as the enumerator, so the two cannot drift.

## The instance ABI

godot-cpp ships no `ScriptInstanceExtension` wrapper, so `src/bs_script_instance.cpp` builds the
`GDExtensionScriptInstanceInfo3` vtable by hand. There is exactly one such vtable in the extension.
Every callback the engine may invoke is filled, and `BSInstance::vtable_is_complete()` is checked at
extension startup: a missing callback stops the language from registering instead of becoming a
null-pointer call at the first question the engine asks.

Three things `ScriptInstance` has in the engine have no slot in the extension ABI. Reified type
arguments and the synthetic flag are stored on `BSInstance` and read back through
`object_get_script_instance`; the RPC configuration is answered at script level by
`BaristaScript::_get_rpc_config`.

## The error channel

A runtime error is reported, never returned: `bs_report_runtime_error` publishes it on the engine's
script-error channel with the function, file and line of the frame that raised it. That is the
channel a runtime transcript is made of, and it is the channel the native cases read back through a
`Logger`.

## The runtime corpus oracle

`evaluate_runtime_case()` in `src/bs_corpus_evaluation.*` is the single definition of a runtime
transcript. It parses and analyzes the case, compiles it, attaches it to a fresh instance of its
declared base type, calls `test()`, and renders everything the engine published while it ran:

```
FS_TEST_OK | FS_TEST_RUNTIME_ERROR | FS_TEST_ANALYZER_ERROR   the status token, always line one
~~ WARNING at line N: (CODE) ...                              analyzer warnings, before the run
<printed lines>                                               print output and error lines,
>> SCRIPT ERROR at <path>:<line> on <func>(): ...             interleaved in emission order
>> ERROR: ...
```

A front-end diagnostic is the whole transcript: the case never reaches the virtual machine, so
`FS_TEST_ANALYZER_ERROR` is followed by the error block and nothing else. Both channels are read
back through one `BaristaScriptCorpusTranscript` logger registered with `OS::add_logger` for the
duration of one case, because emission order is part of the expectation and two separate readers
could not preserve it. The logger is removed in a destructor and its live count is asserted by the
`runtime_corpus` suite: one left registered would append every later case's lines to a dead array
and turn a whole shard's results false while the run still exited zero.

A case the compiler refuses emits `>> COMPILE ERROR: <message>`, a line shape that appears in no
imported `.out` file. That is deliberate. The corpus is pinned as a burndown board, and a family
that is not implemented yet must not be able to coincide with a real expectation and read as a pass.

`scripts/run_corpus_triage.py` supervises the run and owns the exit contract. A residual-failure pin
may absorb exactly one failure: a case that ran and whose transcript diverged. A case that crashed
the host, hung it, or emitted no guarded record did not produce a corpus result, so naming an owner
for it is no answer; those are hard failures whatever the pin says.

## The instruction encoding

A compiled function is a flat `Vector<int>`. Every instruction is its opcode followed by that
opcode's own operands, and each handler's trailing `instruction_pointer += n` is the authoritative
statement of how many words it occupies -- there is no separate table to keep in step.

Two operand shapes exist:

- **Fixed.** The operands follow the opcode directly, and the handler advances by a constant.
- **Counted.** The word after the opcode is how many *address* operands follow; those addresses are
  loaded into the frame's instruction-argument array, and the opcode's own trailing operands come
  after them. Every call, every construction and every literal uses this shape.

An **address word** packs a 24-bit index beside a 2-bit space tag: the frame's stack, the function's
constant pool, or the instance's members. Every producer of an index -- locals, temporaries,
constants and members -- refuses one that does not fit rather than letting it carry into the tag. The nullable flag on a *type* operand also uses bit 24; the two never
meet, because a type operand is a plain integer that the address decoder never reads.

**Jumps are absolute indices into the same `Vector<int>`, and they are patched in place while the
function is being emitted.** Nothing relocates a compiled function afterwards: there is no
serialized form, and no pass reorders or rewrites instructions once `write_end()` has run. A family
that adds a pass which moves instructions has to patch every jump operand with them, which today
means every `write_*` that records a position in one of the emitter's patch lists.

## Lane ownership

The milestone's families work concurrently, separated by file ownership rather than by intention. A
child that needs to edit outside its column opens a pull request against the owning column first.

| Lane | Owns |
| --- | --- |
| **B** core | `src/bs_compiler_expressions.cpp`, `src/bs_compiler_statements.cpp`, `src/bs_compiler_types.cpp`, `src/bs_vm_ops_core.inc`, `src/bs_vm_ops_iterators.inc`, `src/bs_vm_ops_types.inc`, `src/bs_runtime_type.*` |
| **C** glue | `src/barista_script.cpp` / `.h`, `src/bs_script_instance.*`, `src/bs_compiler_class.cpp`, `src/bs_vm_ops_members.inc` |
| **D** async | `src/bs_function_state.*`, `src/bs_lambda_callable.*`, `src/bs_static_self_callable.*`, `src/bs_utility_callable.*`, `src/bs_vm_ops_async.inc`, `src/bs_compiler_lambdas.cpp` |
| **E** linking | `src/bs_conformance_registry.cpp` runtime section, `src/bs_compiler_conformance.cpp`, `src/bs_cache.*` runtime entries, `src/bs_declaration_index.*` load path, `src/barista_script_language.cpp` startup |

`src/bs_function.h` and `src/bs_codegen.h` are shared and frozen, so they belong to no lane.

`CMakeLists.txt` lists every extension source by name while SCons globs `src/*.cpp`, so adding a
file to `src/` changes what one build system compiles and not the other. The lane files therefore
exist already, with their guards and includes in place, so a family adds handlers rather than
restructuring a translation unit.

## Runtime type enforcement

Declared builtin, native and script slots carry a `BSRuntimeType` into compiled functions. Script
identity is a weak `ObjectID`, so the check survives the parser generation without creating a
script/function ownership cycle. A store into `var node: Node` admits null or an object of that
class; a script slot likewise admits only instances of its declaring script or a derived script.

Typed arrays and dictionaries validate and convert their elements at runtime when a value arrives
through `Variant` or an erased container. Conversion is atomic: an invalid element or a converted
dictionary-key collision leaves the destination unchanged. Typed rest parameters use the same
conversion when the VM materializes their erased argument tail.

A slot whose runtime descriptor is owned by later milestone work is refused rather than weakened.
Tuple and union behavior remains reserved for #247, trait receivers for #249, and a type parameter
that needs M5 specialization still fails closed. Every declaration site -- member, local, parameter,
return and loop variable -- routes through `BSCompiler::refuse_unchecked_slot`.

## What the slice does not do yet

Suspension (`await`, coroutines, function state), lambdas and callables, tuples and tagged unions,
statics and inner classes, cross-file bases and preload,
conformance dispatch, and generics. Each is refused by name: the emitter reports the construct it
cannot lower and the script stays invalid, so `_can_instantiate()` is false and `_instance_create()`
yields nothing rather than a half-built instance.

A base that is a GDScript class is refused permanently at this level, not deferred: an object holds
exactly one script instance, and Godot offers no path by which a GDExtension script instance
delegates to a GDScript one.

`super.<method>()` with no base script that declares the method now reports, where reaching the
engine's implementation of a virtual would once have been a silent no-op. It is a deliberate change:
`Object::has_method` and `Object::call` consult the script instance before the engine's own methods,
so forwarding to the owner re-enters the very function the call is delegating out of. `super._init()`
stays a no-op, which is what the engine does for an unimplemented virtual.
