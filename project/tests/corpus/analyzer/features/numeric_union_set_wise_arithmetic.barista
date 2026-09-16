# Set-wise arithmetic: an operator on a union-typed or union-bounded value is checked for every
# combination the set allows, and its result is the union of the per-combination results, which
# collapses to a single type when they all agree.
type NarrowInteger = int | long


func sum[T: NarrowInteger](left: T, right: T) -> String:
	# `int` with `int` stays `int` and every other combination reaches `long`, so the addition is
	# well typed and its result is `int | long`.
	return str(left + right)


func add_numbers[X: Number, Y: Number](left: X, right: Y) -> String:
	# `Number` also allows `int` with `ulong`, which has no common integer type, so `left + right`
	# has no result here. Narrowing both operands to one member first is what makes the addition
	# well typed.
	if left is int:
		if right is int:
			return str(left + right)
	return "unsupported"


func test():
	var left: int | long = 2
	var right: int | long = 3
	# Every combination of `int | long` with `long` promotes to `long`, so this one collapses to a
	# single type.
	var wide: long = 10L
	print(left + wide)
	print(right + wide)

	# Both operands are sets here, so the result is the union `int | long`.
	print(left + right)

	# A float operand keeps the established operator result rules: an integer set still reaches
	# `float` without an explicit conversion.
	print(left + 1.5)

	print(sum(4, 5))
	print(sum(6L, 7L))
	print(add_numbers(8, 9))
	print(add_numbers(8, 9UL))
