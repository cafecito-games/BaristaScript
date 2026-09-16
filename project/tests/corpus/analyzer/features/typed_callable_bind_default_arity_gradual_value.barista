# A gradually typed (Variant) bound value must not narrow the surviving arity range of a bind()
# result: an unknown value stays gradual rather than being treated as a proven mismatch against the
# parameter it would land on at a reduced call arity. Binding an untyped value here still lets the
# base's own trailing defaults widen the result down to a single-argument call.
extends RefCounted


func add_with_defaults(a: int, b: int = 2, c: int = 3) -> int:
	return a + b + c


func test() -> void:
	var gradual_value: Variant = 4
	var bound := Callable(self, "add_with_defaults").bind(gradual_value)
	print(bound.call(1))
