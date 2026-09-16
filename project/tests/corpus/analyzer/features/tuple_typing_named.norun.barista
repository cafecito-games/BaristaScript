# A named tuple is nominal: it is built by calling its declaration, its fields are reachable by
# name and by index, and it erases safely to the unnamed tuple of its element types.
tuple Vec2(x: float, y: float)
tuple Mixed(name: String, int, bool)

func construct() -> Vec2:
	return Vec2(1.0, 2.0)

func read_fields(point: Vec2) -> float:
	var by_name: float = point.x
	var by_index: float = point.1
	return by_name + by_index

func erase(point: Vec2) -> (float, float):
	return point

func mixed_fields(value: Mixed) -> String:
	var name: String = value.name
	var count: int = value.1
	var flag: bool = value.2
	return name + str(count) + str(flag)
