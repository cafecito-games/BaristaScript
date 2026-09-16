# A Callable hidden inside a typed container slot of another Callable's signature must match deeply:
# a required `Callable[[Array[Callable[[int], void]]], void]` is not satisfied by an implementation
# that nests `Callable[[String], void]` inside the array, even though the outer shapes are identical.
trait T:
	abstract func on(handlers: Callable[[Array[Callable[[int], void]]], void]) -> void


class C:
	uses T

	func on(handlers: Callable[[Array[Callable[[String], void]]], void]) -> void:
		pass
