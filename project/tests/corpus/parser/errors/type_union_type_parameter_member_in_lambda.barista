func outer[T](value: T) -> int:
	var inner = func():
		var local: T | int = value
		print(local)
	inner.call()
	return 1
