func get_int() -> Variant:
	return 2


func get_key() -> Variant:
	return "two"


func get_values() -> Variant:
	return [4, 5]


func get_scores() -> Variant:
	return { "three": 3 }


func get_more_scores() -> Variant:
	return { "four": 4 }


func test():
	var values: Array[int] = [1]
	values.append(get_int())
	Utils.check(values.insert(0, get_int()) == OK)
	values.set(1, get_int())
	Utils.check(values.get_typed_builtin() == TYPE_INT)
	Utils.check(str(values) == "[2, 2, 2]")

	values.assign(get_values())
	Utils.check(values.get_typed_builtin() == TYPE_INT)
	Utils.check(str(values) == "[4, 5]")

	var scores: Dictionary[String, int] = { "one": 1 }
	Utils.check(scores.set(get_key(), get_int()))
	Utils.check(scores.get_typed_key_builtin() == TYPE_STRING)
	Utils.check(scores.get_typed_value_builtin() == TYPE_INT)
	Utils.check(scores["two"] == 2)

	scores.assign(get_scores())
	scores.merge(get_more_scores())
	Utils.check(scores.get_typed_key_builtin() == TYPE_STRING)
	Utils.check(scores.get_typed_value_builtin() == TYPE_INT)
	Utils.check(scores.size() == 2)
	Utils.check(scores["three"] == 3)
	Utils.check(scores["four"] == 4)

	print("ok")
