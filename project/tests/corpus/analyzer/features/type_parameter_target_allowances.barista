# Refusing a concrete value in a type-parameter slot is narrow on purpose. It does not touch the
# runtime-checked downcast rule, which stays available whenever the destination type still exists at
# run time, and it does not touch substitution: once a type argument is bound, every check sees the
# concrete type it was bound to rather than the parameter handle.
class List[T]:
	var head: T

	func get_head() -> T:
		return head


# Concrete specialization: `T` is `int` here, so the inherited slot accepts an `int` value.
class IntList extends List[int]:
	func seed() -> int:
		head = 7
		return head


# Rebinding keeps the parameter a parameter, so the inherited `T` slot is satisfied by a `U` value.
class Relay[U] extends List[U]:
	func store(value: U) -> U:
		head = value
		return head


# A parameter bounded by a `final` class denotes exactly that class -- no subtype of it can exist --
# so a value the bound accepts is a value every possible type argument accepts, and the erasure
# argument does not apply.
final class Worker:
	func label() -> String:
		return "worker"


func hire[W: Worker]() -> W:
	return Worker.new()


func identity[X: Number](value: X) -> X:
	# A value already known to be `X` still satisfies an `X` return.
	return value


func widen[X: Number](value: X) -> long:
	# The destination is a concrete type the runtime can name, and the erased value carries what a
	# type test needs, so an erased source still reaches it under a check.
	return value


# A class downcast stays a runtime-checked assignment, because the value itself carries its class.
# Only analyzed, never executed: instantiating a node has no scene tree here.
func no_exec_test():
	var some_node: Node = Node2D.new()
	var narrowed: Node2D = some_node
	print(narrowed)


func test():
	print(identity(3))
	print(widen(4))

	var numbers := IntList.new()
	print(numbers.seed())
	# The inherited `get_head()` returns `int` after substitution.
	var first: int = numbers.get_head()
	print(first)

	var relay := Relay[String].new()
	print(relay.store("kept"))

	print(hire[Worker]().label())
