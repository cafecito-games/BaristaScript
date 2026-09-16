# A Callable nested inside another Callable's signature must match deeply: a required
# `Callable[[Callable[[int], void]], void]` is not satisfied by an implementation that nests
# `Callable[[String], void]`, even though the outer Callable shapes are identical.
trait T:
	abstract func on(handler: Callable[[Callable[[int], void]], void]) -> void


class C:
	uses T

	func on(handler: Callable[[Callable[[String], void]], void]) -> void:
		pass
