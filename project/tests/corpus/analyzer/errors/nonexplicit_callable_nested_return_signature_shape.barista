# The non-explicit Callable path preserves the return signature, not just parameters: a function
# reference whose return is `Callable[[String], void]` cannot satisfy a target whose return is
# `Callable[[int], void]`, even though both outer returns are plain Callables.
func returns_string_cb() -> Callable[[String], void]:
	return func(_s: String) -> void: pass


func test() -> void:
	var handler: Callable[[], Callable[[int], void]] = returns_string_cb
	print(handler)
