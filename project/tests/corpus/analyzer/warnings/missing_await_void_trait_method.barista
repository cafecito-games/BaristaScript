# Flattened trait async void method discarded at root is fire-and-forget; no MISSING_AWAIT.
extends RefCounted
uses Runner

trait Runner:
	async func run() -> void:
		pass

func test() -> void:
	run()
