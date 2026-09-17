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
`src/bs_codegen.h` owns the emitter interface. Both are frozen for the milestone: **an opcode keeps
its number, and a family that needs a new one appends to the end of `BS_OPCODE_LIST` rather than
renumbering.** A compiled function built by one translation unit has to be read the same way by
every other, and a renumbering breaks that silently.

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

## What the slice does not do yet

Suspension (`await`, coroutines, function state), lambdas and callables, tuples and tagged unions,
typed opcodes and typed containers, statics and inner classes, cross-file bases and preload,
conformance dispatch, and generics. Each is refused by name: the emitter reports the construct it
cannot lower and the script stays invalid, so `_can_instantiate()` is false and `_instance_create()`
yields nothing rather than a half-built instance.

A base that is a GDScript class is refused permanently at this level, not deferred: an object holds
exactly one script instance, and Godot offers no path by which a GDExtension script instance
delegates to a GDScript one.
