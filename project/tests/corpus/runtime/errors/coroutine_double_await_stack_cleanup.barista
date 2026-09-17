# Regression for #732: when an async job suspends on a first `await` and then hits a runtime
# error before finishing, `FSFunctionState::~FSFunctionState()` must clear the saved stack
# (double-await path). Before the fix, Variants copied into the coroutine stack leaked.
signal go


async func _job() -> void:
	await go
	var values: Array[int] = [1, 2, 3]
	print(values[10])


func test() -> void:
	_job()
	go.emit()
