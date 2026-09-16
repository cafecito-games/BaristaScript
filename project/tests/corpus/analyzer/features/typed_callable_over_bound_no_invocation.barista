# Over-binding a fixed-arity typed callable is not itself an error: bind()/bindv() are variadic.
# Only invoking the resulting (uninvocable) callable is flagged. Producing an over-bound callable,
# passing it around, and querying non-invoking members such as is_async()/is_null() stays clean.
extends RefCounted


func zero_arg() -> int:
	return 1


func test() -> void:
	var over_bound := Callable(self, "zero_arg").bind(1)
	print(over_bound.is_async())
	print(over_bound.is_null())
	print(over_bound.is_valid())

	# Storing it in a plain Callable slot is allowed; it is still a Callable.
	var slot: Callable = over_bound
	print(slot.is_null())
