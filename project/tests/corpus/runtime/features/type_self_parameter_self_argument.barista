class Base:
	func consume(other: Self) -> void:
		print(other == self)

	func roundtrip() -> Self:
		consume(self)
		var tmp: Self = self
		consume(tmp)
		return self


class Child:
	extends Base


func test() -> void:
	var child := Child.new()
	print(child.roundtrip() is Child)
