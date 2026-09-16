# The gradual rule survives per-member comparison of a trait's union argument: a conformance whose
# alternatives are the same types conforms, one whose alternatives were written in the opposite order
# conforms because they normalize to the same vector, and one that leaves the whole argument on an
# unreified parameter carries no evidence and stays legal.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


class Handler:
	uses Keeper[Callable[[int], void] | int]


class Reordered:
	uses Keeper[int | Callable[[int], void]]


class Open[U]:
	uses Keeper[U]


func take(value: Keeper[Callable[[int], void] | int]) -> void:
	print(value.label())


func test() -> void:
	take(Handler.new())
	take(Reordered.new())
	take(Open.new())
