# Traversing a union's members only separates unions that really differ. An invariant slot still
# accepts a union whose alternatives are the same types, and it still accepts one whose alternatives
# were written in the opposite order, because `make_union()` normalizes them into a canonical order
# before anything compares them. Every dimension the traversal added agrees here as well.
class Keeper[T]:
	var held: T


class Box[E]:
	pass


func take(k: Keeper[Callable[[int], void] | int]) -> String:
	return str(k.held)


func take_async(k: Keeper[AsyncCallable[[int], void] | int]) -> String:
	return str(k.held)


func take_rest(k: Keeper[Callable[[...Array[int]], void] | int]) -> String:
	return str(k.held)


func take_nested(k: Keeper[Box[int] | int]) -> String:
	return str(k.held)


func relay_same(k: Keeper[Callable[[int], void] | int]) -> String:
	return take(k)


func relay_reordered(k: Keeper[int | Callable[[int], void]]) -> String:
	return take(k)


func relay_async(k: Keeper[AsyncCallable[[int], void] | int]) -> String:
	return take_async(k)


func relay_rest(k: Keeper[Callable[[...Array[int]], void] | int]) -> String:
	return take_rest(k)


func relay_nested(k: Keeper[Box[int] | int]) -> String:
	return take_nested(k)


func test() -> void:
	var keeper := Keeper.new()
	keeper.held = 7
	print("same ", relay_same(keeper))
	print("reordered ", relay_reordered(keeper))
	print("async ", relay_async(keeper))
	print("rest ", relay_rest(keeper))
	print("nested ", relay_nested(keeper))
