# A malformed `Coroutine[]` is rejected even when it appears nested as a container element type,
# not just at the top level: the result-type parameter is mandatory wherever `Coroutine[T]` is used.
func test():
	var jobs: Array[Coroutine[]]
