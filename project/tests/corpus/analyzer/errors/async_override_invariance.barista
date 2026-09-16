class AsyncParent:
	async func load_value() -> int:
		return 1

class SyncChild extends AsyncParent:
	func load_value() -> int:
		return 2

class SyncParent:
	func refresh() -> void:
		pass

class AsyncChild extends SyncParent:
	async func refresh() -> void:
		pass

class BodyInferredAsyncChild extends SyncParent:
	func refresh() -> void:
		@warning_ignore("redundant_await")
		await 0

func test():
	pass
