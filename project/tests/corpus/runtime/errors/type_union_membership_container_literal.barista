# An untyped container literal commits to no alternative, since every alternative claims the same
# carrier, so it enters the union with nothing proved. The parameter verifies it when the call runs,
# and contents no alternative describes are rejected there. A literal that *does* satisfy one is
# retyped into it instead (`runtime/features/type_union_membership_container_literal_retyped.fs`).
class Box:
	var label = "box"


class Receiver:
	func absorb(entries: Array[int] | Array[String]) -> void:
		print("absorbed ", entries)

	func drive() -> void:
		absorb([Box.new()])


func test() -> void:
	Receiver.new().drive()
