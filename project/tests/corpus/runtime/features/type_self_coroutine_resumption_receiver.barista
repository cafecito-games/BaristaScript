# A static call that suspends resumes with the receiver it began on, checked by what it builds after
# the suspension rather than by its declared return type. The awaited nested call suspends too, so the
# outer frame's receiver has to survive a suspension that happened inside another frame.
#
# `test()` cannot await anything itself -- the harness calls it once with no main loop -- so each job
# is started, handed to a fire-and-forget drainer that parks on it, and then released by one signal
# emission that resumes the whole chain synchronously.
signal go

var outcome: String = ""


class Base:
	static async func nested(gate: Signal) -> int:
		await gate
		return 1

	static async func spawn(gate: Signal) -> Self:
		await gate
		return Self.new()

	static async func spawn_after_nested(gate: Signal) -> Self:
		var steps := await nested(gate)
		print("nested steps: ", steps)
		return Self.new()


class Derived:
	extends Base


async func drain(handle: Coroutine[Base], label: String) -> void:
	var value := await handle
	outcome = "%s: is derived %s, is base %s" % [label, value is Derived, value is Base]


func test() -> void:
	var suspended: Coroutine[Base] = Derived.spawn(go)
	drain(suspended, "resumed")
	print("before resume: '%s'" % outcome)
	go.emit()
	print(outcome)

	var nested_handle: Coroutine[Base] = Derived.spawn_after_nested(go)
	drain(nested_handle, "nested")
	go.emit()
	print(outcome)

	# The same implementation, resumed through the base handle, still means the base.
	var base_handle: Coroutine[Base] = Base.spawn(go)
	drain(base_handle, "base")
	go.emit()
	print(outcome)
