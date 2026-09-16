func coroutine() -> int:
	@warning_ignore("redundant_await")
	await 0
	return 1

func test():
	await coroutine()
	coroutine()
