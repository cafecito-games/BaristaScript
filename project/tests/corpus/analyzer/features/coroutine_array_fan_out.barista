# Structured-concurrency fan-out: start several async jobs, hold their handles in an
# Array[Coroutine[String]], then await each one. The phantom T threads through the typed-array
# element check, so appending a Coroutine[String] and reading it back as Coroutine[String] both
# type-check.
async func _download(p_file: String) -> String:
	return "got:" + p_file


func test() -> void:
	var files: Array[String] = ["a", "b"]
	var jobs: Array[Coroutine[String]] = []
	for file: String in files:
		jobs.append(_download(file))

	var outputs: Array[String] = []
	for job: Coroutine[String] in jobs:
		outputs.append(await job)

	print(outputs)
