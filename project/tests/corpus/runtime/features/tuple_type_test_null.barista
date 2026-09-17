# A tuple type test is a shape test, so null only passes when the tested tuple type is nullable, and a
# null element only satisfies a nullable element slot. Both mirror `x is Node` being false for null.
func test():
	var missing: Variant = null
	print(missing is (int, String))
	print(missing is (int, String)?)

	var with_null: Variant = [null, 1]
	print(with_null is (RefCounted, int))
	print(with_null is (RefCounted?, int))
	print(with_null is (Variant, int))

	# A nullable element slot only widens the slot by null: a non-null value still has to be of the
	# declared element type, so the slot must not degrade into an untyped one.
	var wrong_element: Variant = [1, 1]
	print(wrong_element is (RefCounted?, int))
