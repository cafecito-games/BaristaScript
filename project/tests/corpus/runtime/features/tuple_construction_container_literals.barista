# A collection literal passed to a tuple field takes the declared container type, so the value is
# built already typed instead of failing at runtime as an untyped Array or Dictionary.
tuple Bag(items: Array[int], lookup: Dictionary[String, int])
tuple Nested(rows: Array[Array[int]], groups: Dictionary[String, Array[int]])

func test():
	var bag := Bag([1, 2], {"a": 1})
	print(bag)
	print(bag.items.get_typed_builtin() == TYPE_INT)
	print(bag.lookup.get_typed_value_builtin() == TYPE_INT)

	var nested := Nested([[1], [2, 3]], {"a": [4]})
	print(nested)
	print(nested.rows.get_typed_builtin() == TYPE_ARRAY)
	print(nested.rows[0].get_typed_builtin() == TYPE_INT)
	print(nested.groups["a"].get_typed_builtin() == TYPE_INT)
