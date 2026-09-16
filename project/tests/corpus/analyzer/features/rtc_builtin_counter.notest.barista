# Companion trait for retroactive_conformance_builtin. Builtin value types are retroactively conformed
# to this trait by an `extend` declaration that supplies required witnesses externally.
trait_name RtcBuiltinCounter

abstract func doubled() -> int

# `Array.size()` returns a 32-bit `int` in its native C++ declaration (see `core/variant/array.h`), so
# this requirement is declared `int`, not `long`, on purpose: it is the width `Array.size()` actually
# satisfies without an explicit witness. See `retroactive_conformance_builtin_wide_size_mismatch.fs`
# for the companion negative case pinning a genuinely wider requirement as a rejected witness.
abstract func size() -> int
