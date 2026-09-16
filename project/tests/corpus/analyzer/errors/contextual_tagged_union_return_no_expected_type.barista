# An untyped return type supplies no union, so a shorthand in return position is rejected.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func produce():
	return .Ok(1)


func test():
	print(produce())
