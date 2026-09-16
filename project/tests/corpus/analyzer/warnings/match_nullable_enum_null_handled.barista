# Handling every declared member and `null` still leaves the integer carrier's undeclared values open,
# so OPEN_ENUM_MATCH_WITHOUT_DEFAULT fires with no unhandled values to name.
enum Direction:
	NORTH = 0
	SOUTH = NORTH + 1

func test():
	var direction: Direction? = Direction.NORTH
	match direction:
		Direction.NORTH:
			print("north")
		Direction.SOUTH:
			print("south")
		null:
			print("null")
	print("ok")
