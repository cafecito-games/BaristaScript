class AsyncParent:
	async func load_value() -> int:
		return 1

class BodyInferredChild extends AsyncParent:
	func load_value() -> int:
		@warning_ignore("redundant_await")
		await 0
		return 2

func test():
	pass
