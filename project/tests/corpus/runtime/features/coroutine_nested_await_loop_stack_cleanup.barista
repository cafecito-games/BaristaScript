# Regression for #829: a coroutine that awaited a nested coroutine chain must not
# double-unref saved stack locals when later loop iterations suspend again.
signal go


async func _case(index: int) -> void:
	if index == 1:
		await _nested_run()
	else:
		await go
	print("case done: ", index)


async func _nested_run() -> void:
	var inner_instances: Dictionary[String, Object] = {}
	inner_instances["probe"] = RefCounted.new()
	await go
	print("nested done, inner=", inner_instances.size())


async func _run_all() -> void:
	var suite_instances: Dictionary[String, Object] = {}
	for index: int in [1, 2, 3]:
		suite_instances[str(index)] = RefCounted.new()
		await _case(index)
	print("all done, suites=", suite_instances.size())


func test() -> void:
	_run_all()
	go.emit()
	go.emit()
	go.emit()
