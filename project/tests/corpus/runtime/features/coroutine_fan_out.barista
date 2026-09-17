# Structured-concurrency fan-out at runtime. Each async job runs its body eagerly when it is
# called (start now), so all jobs have already recorded their start before any handle is awaited.
# The in-flight handles are held in an Array[Coroutine[String]] and awaited later (await later) to
# collect every result in order.
var _started: Array[String] = []


async func _download(p_file: String) -> String:
	_started.append(p_file)
	return "got:" + p_file


func test() -> void:
	var files: Array[String] = ["a", "b", "c"]
	var jobs: Array[Coroutine[String]] = []
	for file: String in files:
		jobs.append(_download(file))

	# Every job body ran at call time, before any await, so the start order is the call order.
	print("started: ", _started)

	var outputs: Array[String] = []
	for job: Coroutine[String] in jobs:
		outputs.append(await job)

	print("outputs: ", outputs)
