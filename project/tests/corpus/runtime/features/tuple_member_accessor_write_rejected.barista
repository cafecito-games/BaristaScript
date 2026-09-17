# A property with an accessor used to accept the corrupt write and fail later, inside its getter,
# leaving the property reading back as `[]`. The setter's own parameter is a tuple slot, so the write
# is now refused where it happens and the property keeps its value, and the refusal is reported with
# the same sentence an in-body tuple store prints.
class Holder extends RefCounted:
	var backed: (int, String) = (0, "zero")

	var field: (int, String):
		get:
			return backed
		set(value):
			backed = value


func test() -> void:
	var holder := Holder.new()
	var object: Object = holder
	object.set("field", (1, 2, 3))
	print(holder.field)
