# A generic tagged union applied with explicit type arguments keeps its `Self` payload fields
# relative to the spelling under the application: an instance base answers identity against that
# base expression (so the frame's own `self` is rejected there), and the unqualified application
# stays relative to the calling frame's receiver.
class Receiver:
	enum Box[T]:
		Empty
		Full(value: T, owner: Self)

	func construct(other: Receiver) -> void:
		var a := other.Box[int].Full(1, other)
		var b := other.Box[int].Full(2, self)
		var c := Box[int].Full(3, self)
		var d := Box[int].Full(4, other)
		print(a, b, c, d)


func test() -> void:
	pass
