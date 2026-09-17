# A hard `Variant` is admitted into a union the way it is admitted into any other typed slot: the
# crossing is booked unsafe and the slot verifies it when it runs.
class Box:
	var label = "box"


func take(v: int | String) -> void:
	print("took ", v)


func test() -> void:
	var loose: Variant = Box.new()
	take(loose)
