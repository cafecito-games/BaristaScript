# A typed array names the type of each element, so an element position in an "Array[int]" expects an
# int and the shorthand is rejected naming that element type.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func test():
	var xs: Array[int] = [.Ok(1)]
	print(xs)
