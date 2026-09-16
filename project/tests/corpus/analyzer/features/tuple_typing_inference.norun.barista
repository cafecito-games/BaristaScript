# An unnamed tuple type is structural: its identity is exactly its element shape, so a literal
# with matching elements satisfies the annotation and a mismatching shape does not.
var pair: (int, String) = (1, "one")
var nested: ((int, int), String) = ((1, 2), "one")

func infer() -> void:
	var inferred := (1, "one")
	var explicit: (int, String) = inferred
	var index_typed: int = explicit.0
	var element_typed: String = explicit.1
	prints(index_typed, element_typed)

func pass_through(value: (int, String)) -> (int, String):
	return value

func nested_index() -> int:
	return nested.0.1
