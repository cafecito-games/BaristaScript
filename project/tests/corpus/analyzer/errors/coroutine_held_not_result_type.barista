# Honest typing closes the root-scope soundness gap: an unawaited async call is a Coroutine[String],
# not a String, so assigning the held handle where a String is expected is a compile error. The old
# flag-over-T representation inferred String here and passed analysis, then misbehaved at runtime.
async func _work() -> String:
	return "x"


func test() -> void:
	var job: Coroutine[String] = _work()
	var text: String = job
	print(text)
