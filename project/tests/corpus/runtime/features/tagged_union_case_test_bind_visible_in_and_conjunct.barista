# A payload bind declared by `is Case(binds)` must be visible to a later `and`-conjunct of the same
# condition, not just to the guarded suite. Regression test for #1301.
enum ConjunctMessage:
	Quit
	Move(x: int, y: int)

func describe(message: ConjunctMessage) -> String:
	if message is ConjunctMessage.Move(x, _) and x > 0:
		return "positive %d" % x
	elif message is ConjunctMessage.Move(x, _):
		return "non-positive %d" % x
	return "quit"

func test():
	print(describe(ConjunctMessage.Move(3, 0)))
	print(describe(ConjunctMessage.Move(-1, 0)))
	print(describe(ConjunctMessage.Quit))

	# The bind is also usable across more than two conjuncts, and still visible in the body.
	var message: ConjunctMessage = ConjunctMessage.Move(5, 6)
	if message is ConjunctMessage.Move(x, y) and x > 0 and y > 0:
		print(x + y)
