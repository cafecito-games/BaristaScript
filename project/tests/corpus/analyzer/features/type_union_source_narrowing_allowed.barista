# Rejecting an unverifiable integer-width narrowing out of a union does not close the downcast rule
# itself. Each alternative is judged by the rule that governs the concrete assignment it stands for, so
# a widening alternative still passes, an alias still carries the member's declared width, a nullable
# union still widens, and an alternative the runtime can tell apart is still narrowed under a check.
type Scalar = int | long
type Wide = long
type MaybeScalar = int? | long
type ScalarOrText = int | long | String


func take_long(value: long) -> long:
	return value


# A class downcast stays a runtime-checked assignment, because the value itself carries its class.
# Only analyzed, never executed: instantiating a node has no scene tree here.
func no_exec_test():
	var some_node: Node = Node2D.new()
	var narrowed: Node2D = some_node
	print(narrowed)


func test():
	# Every alternative of `int | long` fits `long`, so the whole set does.
	var scalar: Scalar = 5
	var widened: long = scalar
	print(widened)
	print(take_long(scalar))

	# The alias collapses to `long` and keeps that width, so the wide literal survives.
	var collapsed: Wide = 2147483648L
	print(collapsed)

	# Nullability is hoisted onto the union and the widening target keeps accepting it.
	var maybe: MaybeScalar = 9
	var maybe_widened: long? = maybe
	print(maybe_widened)

	# Both numeric alternatives fit `long` and the runtime check still distinguishes a `String`, so the
	# downcast stays available.
	var mixed: ScalarOrText = 11
	var mixed_widened: long = mixed
	print(mixed_widened)
