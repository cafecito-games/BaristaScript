# An async static lambda retains the captured receiver across suspend/resume. The lambda is created in
# a static frame, escapes it, and only then is started; it suspends on a signal and constructs `Self`
# after the resume. The receiver it sees on resume is the one it was created with, never the resuming
# caller's context. `test()` runs once with no main loop, so a single signal emission resumes the
# whole chain synchronously (same drain pattern as type_self_coroutine_resumption_receiver).
signal go

var outcome: String = ""


class Base:
	static func make_async_spawner() -> Callable:
		return func(gate: Signal) -> Self:
			await gate
			return Self.new()


class Child:
	extends Base


# Awaiting the escaped lambda inside an async runner is what starts its coroutine. The captured
# receiver must survive the lambda's own suspension and be the one `Self.new()` runs against.
async func run(factory: Callable, gate: Signal, label: String) -> void:
	var value := await factory.call(gate)
	outcome = "%s: is child %s" % [label, value is Child]


func test() -> void:
	run(Child.make_async_spawner(), go, "child")
	print("before resume: '%s'" % outcome)
	go.emit()
	print(outcome)

	# The base receiver survives suspend/resume too: the same compiled lambda, created through the
	# base handle, still means the base after it resumes.
	run(Base.make_async_spawner(), go, "base")
	go.emit()
	print(outcome)
