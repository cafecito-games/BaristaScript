# An assignment to a variable annotated with a non-union type expects that type, so the shorthand is
# rejected with the type the position expects rather than a suggestion to annotate an already
# annotated target.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func test():
	var x: int = 0
	x = .Ok(1)
	print(x)
