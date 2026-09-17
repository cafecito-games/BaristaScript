signal go

enum AsyncStatus:
	READY = 41

	async func describe_after(resume_signal: Signal) -> String:
		await resume_signal
		return "async-instance:" + str(self)

	static async func load() -> Self:
		return READY


async func _runner() -> void:
	var loaded: AsyncStatus = await AsyncStatus.load()
	print(loaded)
	print(await loaded.describe_after(go))


func test() -> void:
	_runner()
	print("before resume")
	go.emit()
	print("after resume")
