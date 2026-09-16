# Each alternative of a type union is admitted by the rules it would carry standing alone: a
# `Self`-bearing tuple alternative accepts the calling frame's own receiver, the plain alternative
# accepts its own values, a nullable union accepts `null`, and a `final` binding needs no identity
# because it names exactly one class.
final class Sealed:
	func attach(link: int | (int, Self)) -> String:
		return str(link)


class Receiver:
	func attach(link: int | (int, Self)) -> String:
		return str(link)

	func attach_optional(link: (int, Self) | String?) -> String:
		return str(link)

	func attach_carrier(link: int | (int, Array[Self])) -> String:
		return str(link)

	func drive() -> void:
		print("plain ", attach(7))
		print("identity ", self.attach((1, self)) != "")
		print("optional null ", attach_optional(null))
		print("optional other ", attach_optional("text"))
		print("carrier ", attach_carrier((1, [self])) != "")


func test() -> void:
	Receiver.new().drive()
	var sealed := Sealed.new()
	print("final ", sealed.attach((2, sealed)) != "")
