# Every value-flow position with a union destination answers the same question through the same
# membership test, so a value that satisfies an alternative flows through all of them unchanged and
# adds no diagnostic: argument, return, declared local, later assignment, member store, typed
# collection element, signal emission, and a tagged-union payload field.
signal reported(value: int | String)


enum Carried:
	Holds(value: int | String)


var member: int | String = 0


func take(value: int | String) -> int | String:
	return value


func test() -> void:
	var local: int | String = take(7)
	print("argument and return ", local)

	local = "seven"
	print("assignment ", local)

	member = local
	print("member ", member)

	var entries: Array[Variant] = [local]
	print("element ", entries)

	reported.emit(local)

	var carried := Carried.Holds(local)
	print("payload ", carried)
