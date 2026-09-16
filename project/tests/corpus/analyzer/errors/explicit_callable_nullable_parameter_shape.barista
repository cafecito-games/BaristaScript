# The explicit Callable signature path stays strict about nullability: a `Callable[[Node?], void]`
# source is not compatible with a `Callable[[Node], void]` target. This guards the explicit-path
# comparison, which distinguishes nullable parameter slots, from being loosened.
func test() -> void:
	var source: Callable[[Node?], void] = func(_node: Node?) -> void:
		pass
	var target: Callable[[Node], void] = source
	print(target)
