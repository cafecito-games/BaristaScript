# An untyped parameter supplies no union, so a shorthand in that argument position is rejected.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func take(value):
	print(value)


func test():
	take(.Ok(1))
