# A named function whose return type is body-inferred (no annotation) flows through the non-explicit
# Callable path. Its inferred return must be treated as the erased Variant the MethodInfo path used,
# not as the concrete type it happened to infer, so it cannot silently satisfy a target whose return is
# concretely typed `int`.
func inferred_int():
	return 1


func test() -> void:
	var typed_handler: Callable[[], int] = inferred_int
	print(typed_handler)
