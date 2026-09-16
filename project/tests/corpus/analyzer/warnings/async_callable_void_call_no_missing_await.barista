# AsyncCallable[[...], void].call() at root is a void fire-and-forget launch; no MISSING_AWAIT.
async func _work(_value: int) -> void:
	pass


func test() -> void:
	var handler: AsyncCallable[[int], void] = _work
	handler.call(7)
