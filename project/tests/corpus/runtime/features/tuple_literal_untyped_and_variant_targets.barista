# A target that declares no element typing supplies none, so the literal is built exactly as it was
# before: an unannotated container element stays untyped and inference is unchanged.
func test():
	var untyped_element: (int, Array) = (1, [])
	print(untyped_element)
	print(untyped_element[1].get_typed_builtin() == TYPE_NIL)

	var as_variant: Variant = (1, [])
	print(as_variant)

	var inferred := (1, [])
	print(inferred)
	print(inferred[1].get_typed_builtin() == TYPE_NIL)
