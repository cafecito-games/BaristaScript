# The array and dictionary patchers validate every element, so a `Self` return can trust them and skip
# the contract check. The tuple patcher reports nothing by design, so a tuple literal must not be able
# to claim that validation: the `Self` return contract still has to reject a wrong element here, and it
# still has to accept the right one.
class Base:
	func wrong_pair() -> (Self, int):
		return (Base.new(), 1)

	func right_pair() -> (Self, int):
		return (self, 1)


func test() -> void:
	pass
