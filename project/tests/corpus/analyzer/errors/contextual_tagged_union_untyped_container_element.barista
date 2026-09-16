# An untyped container literal has no element type, so it supplies no union to an element written as
# a contextual case shorthand.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func test():
	var values = [.Ok(1)]
	print(values)
