# The leading-`.` contextual case shorthand parses in every expression position, with and without a
# payload. No declaration below supplies a union, so every shorthand is reported as missing its
# expected tagged-union type, including the ones nested in a container literal or in the branches of
# a conditional expression. A shorthand in a position that supplies no type at all is reported once
# every body is resolved, which is why the container elements are reported after the conditional.
enum Result[T, E]:
	Ok(value: int)
	Err(error: String)


enum Option[T]:
	None
	Some(value: int)


func run(condition: bool) -> void:
	var single = .Ok(1)
	var empty = .None
	var elements = [.Ok(1), .Err("x")]
	var chosen = .Ok(1) if condition else .Err("no")
	print(single, empty, elements, chosen)
