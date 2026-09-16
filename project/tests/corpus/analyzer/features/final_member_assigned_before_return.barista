# Assigning the blank final before an early `return` satisfies definite
# assignment on that exit path, so the object never escapes with a blank final.
class Guarded:
	final var id: int

	func _init(valid: bool) -> void:
		if not valid:
			id = 0
			return
		id = 1
		print("id is %d" % id)

func test() -> void:
	var _guard := Guarded.new(true)
	var _bail := Guarded.new(false)
