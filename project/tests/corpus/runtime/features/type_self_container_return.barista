class Holder:
	func items() -> Array[Self]:
		return [self]

	func mapping() -> Dictionary[String, Self]:
		return { "item": self }


func test() -> void:
	var holder := Holder.new()
	print(holder.items()[0] is Holder)
	print(holder.mapping()["item"] is Holder)
