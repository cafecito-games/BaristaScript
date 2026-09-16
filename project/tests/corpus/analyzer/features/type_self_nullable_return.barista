class Holder:
	func maybe_self(value: bool) -> Self?:
		if value:
			return self
		return null


func test() -> void:
	var holder := Holder.new()
	print(holder.maybe_self(true) is Holder)
	print(holder.maybe_self(false) == null)
