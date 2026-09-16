# An unawaited async call is an honest Coroutine[T]: it can be held in an inferred variable and
# awaited later to yield T. Holding it (not discarding it as a statement) raises no missing-await
# diagnostic, which is what enables store-and-await-later patterns.
async func _work(value: int) -> String:
	return str(value)


func test() -> void:
	var job := _work(7)
	var result: String = await job
	print(result)
