# An application spells the declaration's full parameter vector, and a union with no parameters
# takes no arguments at all.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)

enum Plain:
	Value(value: int)

func too_few(value: Result[int]) -> void:
	print(value)

func too_many(value: Result[int, String, float]) -> void:
	print(value)

func not_generic(value: Plain[int, String]) -> void:
	print(value)
