# A naive `n - D - K` widening of the surviving arity range would accept this call, but it is
# unsound: at the reduced arity the bound int value would land in the middle "q: String" parameter
# instead of the trailing default "s: int" parameter it was checked against, which is a type
# mismatch. The extra arity must only survive when the bound value still fits at the parameter
# position it shifts into, so this call stays rejected.
extends RefCounted


func mixed_types(p: int, q: String, s: int = 1) -> String:
	return q + str(p + s)


func test() -> void:
	var bound := Callable(self, "mixed_types").bind(2)
	print(bound.call(9))
