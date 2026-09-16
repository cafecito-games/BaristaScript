# A case-bind test written inside a lambda body that itself is embedded in an if/while/assert
# condition is not an `and`-conjunct of that outer condition: it must be rejected exactly like
# any other illegal-position bind, and must not leak into the lambda's own suite as a declared
# local. Regression test for #1301.
enum LambdaLeakCaseMessage:
	Quit
	Move(x: int, y: int)

func test():
	var message: LambdaLeakCaseMessage = LambdaLeakCaseMessage.Quit
	if message is LambdaLeakCaseMessage.Quit and (func() -> bool:
		var matched: bool = message is LambdaLeakCaseMessage.Move(x, y)
		print(x)
		return matched
	).call():
		print("quit and checked")
