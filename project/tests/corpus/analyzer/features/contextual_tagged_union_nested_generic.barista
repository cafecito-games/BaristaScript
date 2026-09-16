# Container element types are patched recursively, so a shorthand resolves at every nesting depth
# with the specialization that depth carries.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


func test():
	var grid: Array[Array[Result[int, String]]] = [[.Ok(1)], [.Err("x")]]
	print(grid)

	var groups: Dictionary[String, Array[Result[int, String]]] = {"a": [.Ok(1), .Err("x")]}
	print(groups)

	var rows: Array[Dictionary[int, Result[int, String]]] = [{1: .Ok(1)}]
	print(rows)

	# Each depth carries its own specialization, so the inner shorthand is checked against the inner
	# payload type rather than the outer one.
	var flipped: Array[Array[Result[String, int]]] = [[.Ok("one"), .Err(2)]]
	print(flipped)
