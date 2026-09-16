# A binding to a declaring generic's own parameter records nothing concrete, and an argument-erased
# instance carries no evidence of its own. Both are an absence of evidence, so every specialized
# store stays accepted.
trait RcjKeeper[T]:
	abstract func size() -> int


class RcjGeneric[U]:
	uses RcjKeeper[U]

	func size() -> int:
		return 1


func test() -> void:
	var specialized: RcjKeeper[String] = RcjGeneric.new()
	var raw: RcjKeeper = RcjGeneric.new()
	print(specialized != null, raw != null)
