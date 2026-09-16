# A path that returns early from `_init()` still constructs the object, so a
# blank final left unassigned on that path escapes at its default value. Like a
# Java blank final in a constructor, it must be assigned before every return.
class Guarded:
	final var id: int

	func _init(valid: bool) -> void:
		if valid:
			id = 1
		else:
			return
		print("id is %d" % id)

func test() -> void:
	var _guard := Guarded.new(true)
