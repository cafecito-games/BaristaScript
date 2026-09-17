# A named call that skips a middle parameter inlines that parameter's default at
# the call site, resolved against the STATICALLY known callee signature. This is
# resolved entirely at compile time, like the rest of the feature: the call is
# type-checked against the receiver's static type, and the static type's default
# is baked in. This mirrors C#'s optional arguments, whose default values are
# likewise determined by the compile-time receiver type, not the runtime type.
#
# A trailing omission is different: the callee fills it via the runtime default
# mechanism (the only ABI-free way to honor a non-constant default), so a
# base-typed receiver dispatched to an override picks up the override's default
# there. The two paths therefore diverge when a subclass overrides a constant
# default with a different value; this is intended and documented behavior, an
# inevitable consequence of the "no ABI change" non-goal of the feature.
# https://github.com/cafecito-games/godot/issues/454

class Base:
	func f(a: int, b: int = 10, c: int = 0) -> int:
		return a * 100 + b * 10 + c

class Derived extends Base:
	func f(a: int, b: int = 99, c: int = 0) -> int:
		return a * 100 + b * 10 + c


func test():
	var base_typed: Base = Derived.new()

	# Middle gap on a base-typed receiver: `b` is inlined from Base's static
	# default (10), even though dispatch reaches Derived.f at runtime. 1*100 + 10*10 + 5.
	print(base_typed.f(1, c = 5))  # 205

	# Trailing omit on the same receiver: the callee (Derived.f) supplies its own
	# runtime default for `b` (99). 1*100 + 99*10 + 0.
	print(base_typed.f(1))  # 1090

	# When the static type is Derived, the middle-gap inline uses Derived's
	# default (99), matching dispatch. 1*100 + 99*10 + 5.
	var derived_typed := Derived.new()
	print(derived_typed.f(1, c = 5))  # 1095

	# No override in play: middle-gap inline and trailing omit agree.
	var plain := Base.new()
	print(plain.f(1, c = 5))  # 205
	print(plain.f(1))         # 200
