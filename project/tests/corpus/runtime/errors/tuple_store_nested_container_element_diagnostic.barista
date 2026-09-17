# Where a rejected tuple store points when the element that disagreed sits inside a nested tuple.
# Naming only the outermost declaration leaves the reader to work out which position failed, so the
# clause names the whole path down to the culprit and keeps describing the same single cause.
#
# The walk stops wherever the story stops being a single element's container typing: the last case
# fails on two elements of the nested tuple at once -- a null in an element whose declaration is not
# nullable, which a tuple test rejects, plus an untyped array -- so the message stays in its plain form.
func keep_nested(value) -> Variant:
	var kept: (int, (int, Array[int])) = value
	return kept


func keep_deeply_nested(value) -> Variant:
	var kept: (int, (int, (String, Array[int]))) = value
	return kept


func keep_nested_with_second_failure(value) -> Variant:
	var kept: (int, (Node, Array[int])) = value
	return kept


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	print("nested: ", keep_nested(supply((1, (2, [])))))
	print("deeply nested: ", keep_deeply_nested(supply((1, (2, ("a", []))))))
	print("nested second failure: ", keep_nested_with_second_failure(supply((1, (null, [])))))
