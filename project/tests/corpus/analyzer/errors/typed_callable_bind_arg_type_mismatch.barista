# Making bind() variadic only relaxes the arity check; bound arguments that line up with known
# target parameters are still type-checked. Binding a String where the target parameter is int
# is rejected.
extends RefCounted


func one(value: int) -> int:
	return value


func test() -> void:
	var single := Callable(self, "one")
	print(single.bind("not an int"))
