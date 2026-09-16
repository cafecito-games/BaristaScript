class Box[T]:
	var stored: T

	func get_value() -> T:
		return stored

	func size() -> int:
		return 1


func assign_from_raw(box: Box) -> void:
	var local: int = box.get_value()
	print(local)


func assign_from_specialized(box: Box[int]) -> void:
	var local: int = box.get_value()
	print(local)


func return_from_raw(box: Box) -> int:
	return box.get_value()


func return_from_specialized(box: Box[int]) -> int:
	return box.get_value()


func return_independent_member(box: Box) -> int:
	return box.size()


class HolderSame[T]:
	# A parameter named like the raw receiver's is still a different parameter, so the value the raw
	# receiver hands over does not satisfy this frame's return type either.
	func return_collision(box: Box) -> T:
		return box.get_value()
