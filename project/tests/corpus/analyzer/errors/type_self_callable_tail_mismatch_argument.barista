# The `Self` contract decides a callable argument by structural identity, so a tail the parameter
# declares is part of what the argument must supply: a tail whose element type is unrelated is
# rejected, and so is one narrower than `Self`, since the receiver may resolve to a sibling leaf that
# is not that subclass. A fixed-arity callable is rejected where the parameter declares a tail it will
# invoke. A tail the parameter never invokes is admitted instead, but that argument still carries
# `Self` in its own fixed prefix, so it answers to receiver identity and reaches only the calling
# frame's own receiver. A `Self` nested inside a specialization is invariant there, so widening it to
# the bound is refused even though widening a bare `Self` element is not.
class Box[T]:
	var value: T


class Cell:
	func self_tail(_callback: Callable[[...Array[Self]], void]) -> void:
		pass

	func self_tail_declared(_callback: Callable[[Self, ...Array], void]) -> void:
		pass

	func self_fixed(_callback: Callable[[Self], void]) -> void:
		pass

	func route(other: Cell, wrong: Callable[[...Array[String]], void], fixed: Callable[[Self], void]) -> void:
		other.self_tail(wrong)
		self_tail(wrong)
		other.self_tail_declared(fixed)
		self_tail_declared(fixed)

	func route_relaxed(other: Cell, variadic: Callable[[Self, ...Array], void]) -> void:
		other.self_fixed(variadic)
		self_fixed(variadic)

	func route_narrow_tail(other: Cell, narrow: Callable[[...Array[Leaf]], void]) -> void:
		other.self_tail(narrow)
		self_tail(narrow)

	func self_boxed_tail(_callback: Callable[[...Array[Box[Self]]], void]) -> void:
		pass

	func route_nested_specialization(other: Cell, boxed: Callable[[...Array[Box[Cell]]], void]) -> void:
		other.self_boxed_tail(boxed)
		self_boxed_tail(boxed)


class Leaf:
	extends Cell


func test() -> void:
	pass
