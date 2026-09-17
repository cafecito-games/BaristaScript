# Named arguments bind to parameters by name, but the argument *expressions* must
# still be evaluated in source (left-to-right written) order, matching Python, C#,
# and Kotlin. Canonicalizing a named call into positional order for the callee
# must not reorder the observable side effects of evaluating each argument.
# https://github.com/cafecito-games/godot/issues/447

var order: Array = []


func note(label: String, value: int) -> int:
	order.append(label)
	return value


func take(a: int, b: int, c: int) -> void:
	prints(a, b, c)


func take_default(a: int, b: int = 10, c: int = 20) -> void:
	prints(a, b, c)


func test():
	# Pure named, reverse of declaration order: arguments bind a=1, b=2, c=3, but
	# the side effects must run in written order c, b, a.
	order.clear()
	take(c = note("c", 3), b = note("b", 2), a = note("a", 1))
	prints("eval", order)

	# Mixed positional + named: the positional `a` is written first, then named
	# `c`, then named `b`. Side effects run a, c, b even though binding is a, b, c.
	order.clear()
	take(note("a", 1), c = note("c", 3), b = note("b", 2))
	prints("eval", order)

	# A skipped middle parameter inlines a constant default at the call site; that
	# synthesized constant has no side effects and must not disturb the source
	# order of the real arguments, which run c then a.
	order.clear()
	take_default(c = note("c", 3), a = note("a", 1))
	prints("eval", order)
