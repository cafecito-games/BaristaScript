# `self` evaluates to the frame's receiver, so it is typed `Self` in every body of a class, whatever
# else the enclosing function happens to mention. The two callers below differ only by an unused
# `Self`-typed local and agree about every use of `self`: a local initializer, a container literal, a
# call argument, and a return value. The receiver is a subclass instance, so every position resolves
# to the runtime leaf rather than to the class the body is written in.
class Receiver:
	var linked: Self? = null

	func take_pair(pair: (int, Self)) -> void:
		print("pair ", pair.0, " ", pair.1 == self)

	func take_items(items: Array[Self]) -> void:
		print("items ", items[0] == self)

	func plain_caller() -> Self:
		var direct: Self = self
		var items: Array[Self] = [self]
		take_items(items)
		take_pair((1, self))
		linked = direct
		return self

	func mentioning_caller() -> Self:
		var _unrelated: Array[Self] = []
		var direct: Self = self
		var items: Array[Self] = [self]
		take_items(items)
		take_pair((2, self))
		linked = direct
		return self

	static func make() -> Self:
		return Self.new()


class Sub:
	extends Receiver


func test() -> void:
	var receiver := Sub.new()
	print(receiver.plain_caller() == receiver)
	print(receiver.mentioning_caller() == receiver)
	print(receiver.linked == receiver)
	print(Sub.make() is Sub)
