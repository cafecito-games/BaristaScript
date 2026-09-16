# A contextual case shorthand in a var/const initializer resolves against the declared type, and
# produces exactly the value the explicit spelling produces.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)


enum Status:
	Idle
	Busy(reason: String)


func test():
	var ok: Result[int, String] = .Ok(1)
	var err: Result[int, String] = .Err("bad")
	print(ok)
	print(err)
	print(ok == Result[int, String].Ok(1))

	const FIXED: Result[int, String] = .Ok(7)
	print(FIXED)

	# A non-generic union works the same way.
	var busy: Status = .Busy("waiting")
	print(busy)

	# The payload is checked against the applied specialization, so a collection literal in payload
	# position is typed by the field type just as the explicit form types it.
	var flipped: Result[String, int] = .Ok("one")
	print(flipped)
