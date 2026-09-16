# An `as` cast names the type its operand is expected to have, so it qualifies a contextual case
# shorthand in operand position.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


enum Option[T]:
	None
	Some(value: T)


func test():
	var labeled = .Ok(1) as Result[int, String]
	print(labeled)
	print(labeled == Result[int, String].Ok(1))

	var empty = .None as Option[int]
	print(empty)

	# The cast type qualifies a nested shorthand through the payload field type as well.
	var nested = .Ok(.Ok(1)) as Result[Result[int, String], String]
	print(nested)
