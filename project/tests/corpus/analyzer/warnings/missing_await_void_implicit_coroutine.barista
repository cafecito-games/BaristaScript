# A non-async void function that contains await is still a Coroutine[void]; root discard is silent.
func work() -> void:
	@warning_ignore("redundant_await")
	await 0

func test() -> void:
	work()
