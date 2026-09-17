# A coroutine result may be a class handle too, so awaiting one yields a usable `Type[T]`.
async func _provide() -> Type[Node]:
	return Node


func test() -> void:
	var job: Coroutine[Type[Node]] = _provide()
	var handle: Type[Node] = await job
	print(handle == Node)
