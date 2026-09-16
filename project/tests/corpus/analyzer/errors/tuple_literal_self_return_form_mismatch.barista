# The `Self` return contract is skipped only when an element-validating patcher actually ran. A literal
# whose form does not match the declared return type is patched by nothing, so the contract still has
# to report it: an array written for a tuple return type, and a dictionary written for an array one.
class Base:
	func array_for_tuple() -> (Self, int):
		return [Base.new(), 1]

	func dictionary_for_array() -> Array[Self]:
		return {"a": Base.new()}


func test() -> void:
	pass
