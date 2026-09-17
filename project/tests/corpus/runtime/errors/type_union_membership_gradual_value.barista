# A soft local carries a value and no promise, so a union destination it reaches proves nothing about
# membership. The declared local verifies it when the store runs.
class Box:
	var label = "box"


func loose_value():
	return Box.new()


func test() -> void:
	var soft = loose_value()
	var slot: int | String = soft
	print("slot ", slot)
