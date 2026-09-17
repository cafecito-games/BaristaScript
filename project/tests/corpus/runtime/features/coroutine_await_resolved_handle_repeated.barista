# Regression for #1944 (idempotent repeated await): once a coroutine handle is resolved, every
# subsequent `await` of it yields the identical latched result. The drain runner awaits the same
# resolved handle three times; all three awaits must print the same value without parking.
signal go


async func _job() -> String:
	await go
	return "result"


async func _drain(p_handle: Coroutine[String]) -> void:
	print("await one: ", await p_handle)
	print("await two: ", await p_handle)
	print("await three: ", await p_handle)
	print("drain finished")


func test() -> void:
	_drain(_job())
	go.emit()
