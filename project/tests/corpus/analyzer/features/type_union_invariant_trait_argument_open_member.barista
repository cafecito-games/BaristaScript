# Union members are ordered by their rendered spelling, so a member left on an unreified type
# parameter can move once it is substituted: `Array[U] | Array[int]` sorts the open member first, yet
# at `U == long` the same conformance is exactly `Array[int] | Array[long]`. Pairing the two vectors
# by index would invent a contradiction between `Array[int]` and `Array[long]`, so a vector carrying
# an open member is reconciled as a set instead and stays legal.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


class Reorderable[U]:
	uses Keeper[Array[U] | Array[int]]


func take(value: Keeper[Array[int] | Array[long]]) -> void:
	print(value.label())


func relay[U](value: Reorderable[U]) -> void:
	take(value)


func test() -> void:
	take(Reorderable.new())
	relay[long](Reorderable.new())
