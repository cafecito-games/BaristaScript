# A named tuple field whose declared type is not a tagged union supplies no union to a contextual
# case shorthand written there, so the shorthand stays unqualified and is reported.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


tuple Slot(index: int, label: String)


func test():
	var slot := Slot(.Ok(1), "x")
	print(slot)
