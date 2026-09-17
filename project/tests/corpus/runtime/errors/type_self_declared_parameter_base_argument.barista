# The isolation control for the rest-tail rejection: a declared `Self` parameter invoked through the
# subclass rejects a base instance with the same message shape the rest tail now produces.
class Base:
	static func take(_one: Self) -> void:
		print("body ran")


class Child extends Base:
	pass


func test() -> void:
	print("before")
	var handle: Variant = Child
	handle.take(Base.new())
