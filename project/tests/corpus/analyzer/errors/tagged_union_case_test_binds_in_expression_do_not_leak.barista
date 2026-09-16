# Case-payload binds parsed in an illegal position (an ordinary expression statement, not the
# condition of an if/elif/while/assert) must not leak into the rest of the suite as declared
# locals: `x` must still be undeclared afterward. Regression test for #1301.
enum LeakCaseMessage:
	Quit
	Move(x: int, y: int)

func test():
	var message: LeakCaseMessage = LeakCaseMessage.Quit
	var matched: bool = message is LeakCaseMessage.Move(x, y)
	print(matched)
	print(x)
