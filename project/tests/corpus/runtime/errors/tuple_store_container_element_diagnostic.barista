# What a rejected tuple store says about a container element. Naming the whole tuple type alone leaves
# the reader unable to tell that the arity and every other element agreed, so the message adds a clause
# whenever exactly one element failed and failed only on its own element typing.
#
# The advice differs by what the value carries. An untyped array has to acquire element typing, while an
# array typed as something else already has typing and simply disagrees. And when a second element fails
# too -- here a null in an element whose declaration is not nullable, which a tuple test rejects -- the
# clause would describe only part of the story, so the message stays in its plain form.
func keep_untyped(value) -> Variant:
	var kept: (int, Array[int]) = value
	return kept


func keep_wrongly_typed(value) -> Variant:
	var kept: (int, Array[int]) = value
	return kept


func keep_with_second_failure(value) -> Variant:
	var kept: (Node, Array[int]) = value
	return kept


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var typed_as_string: Array[String] = ["2"]
	print("untyped: ", keep_untyped(supply((1, [2]))))
	print("wrongly typed: ", keep_wrongly_typed(supply((1, typed_as_string))))
	print("second failure: ", keep_with_second_failure(supply((null, [2]))))
