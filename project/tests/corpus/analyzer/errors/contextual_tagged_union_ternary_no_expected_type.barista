# An inferred declaration supplies no union to the conditional, so neither branch has one to be
# qualified with and each shorthand is reported where it is written.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func test(condition: bool = true):
	var chosen = .Ok(1) if condition else .Err("x")
	print(chosen)
