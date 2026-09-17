final class Thing:
	static func make() -> Self:
		return Thing.new()

	static func make_items() -> Array[Self]:
		return [Thing.new()]


func test() -> void:
	var thing: Thing = Thing.make()
	print(thing is Thing)
	var things: Array[Thing] = Thing.make_items()
	print(things[0] is Thing, " ", things.get_typed_script() == Thing)
