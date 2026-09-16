# An untyped lambda parameter (Variant) flows through the structural signature comparison once the
# non-explicit Callable path is selected, but it must not silently satisfy a concretely typed slot:
# strict slot equality preserves the rejection the MethodInfo comparison path already enforced.
func test() -> void:
	var untyped := func(_x): pass
	var typed_handler: Callable[[int], void] = untyped
	print(typed_handler)
