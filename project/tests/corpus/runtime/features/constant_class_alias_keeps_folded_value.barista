# A class-valued `const` emits the class it folded to, which is not always the class its inferred
# type names: a ternary over two class handles types as one branch while folding to the other. The
# stored value is the binding, so constructing through the alias builds that class.
class Left:
	func label() -> String:
		return "left"


class Right:
	func label() -> String:
		return "right"


const Picked = Left if false else Right


func test() -> void:
	print(Picked.new().label())
