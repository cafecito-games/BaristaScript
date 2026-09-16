# The gradual rule survives per-component comparison: only a component both sides fix and disagree on
# rejects. A composite whose fixed components agree is accepted whatever its open component is, a
# conformance forwarded through a method-scope parameter still carries no evidence, and a destination
# that differs only in a component the conformance leaves open stays legal.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


class Pair[A, B]:
	pass


class Partial[U]:
	uses Keeper[Pair[int, U]]


func fine(value: Partial[float]) -> Keeper[Pair[int, float]]:
	return value


func open(value: Partial) -> Keeper[Pair[int, float]]:
	return value


func forwarded[X](value: Partial[X]) -> Keeper[Pair[int, X]]:
	return value


func test() -> void:
	print(fine(Partial[float].new()).label())
	print(open(Partial[float].new()).label())
	print(forwarded(Partial[String].new()).label())

	# A raw conformer leaves the second component on its unreified parameter, so a destination that
	# differs only there contradicts nothing and the store stays legal.
	var open_component: Keeper[Pair[int, String]] = Partial.new()
	print(open_component.label())
