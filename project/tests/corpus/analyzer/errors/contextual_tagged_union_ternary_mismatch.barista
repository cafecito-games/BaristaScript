# A branch of the conditional is checked against the union the conditional is expected to have, so a
# payload that does not fit the applied specialization is reported like any other case construction.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func test(condition: bool = true):
	var chosen: Result[int, String] = .Ok(1) if condition else .Err(5)
	print(chosen)
