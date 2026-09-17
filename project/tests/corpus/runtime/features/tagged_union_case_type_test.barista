# `is` against a tagged-union case tests the runtime tag and, with a bind list, extracts the payload
# into locals scoped to the guarded suite. `_` skips a payload position and a bare case name is a
# plain tag test.
enum CaseMessage:
	Quit
	Move(x: int, y: int)
	Write(text: String)

enum CaseLevel:
	Low = 1
	High = 7

func describe(message: CaseMessage) -> String:
	if message is CaseMessage.Move(x, y):
		return "move %d,%d" % [x, y]
	elif message is CaseMessage.Write(text):
		return "write " + text
	elif message is CaseMessage.Quit:
		return "quit"
	return "unknown"

func first_text(messages: Array) -> String:
	var index: int = 0
	while index < messages.size():
		var candidate: Variant = messages[index]
		index += 1
		if candidate is CaseMessage.Write(text):
			return text
	return ""

func test():
	var move: CaseMessage = CaseMessage.Move(1, 2)
	var write: CaseMessage = CaseMessage.Write("hi")
	var quit: CaseMessage = CaseMessage.Quit

	print(describe(move))
	print(describe(write))
	print(describe(quit))

	# `_` skips a payload position without declaring a local.
	if move is CaseMessage.Move(_, y):
		print(y)

	# A bare case name is a tag test; a payload case still checks the payload arity.
	print(move is CaseMessage.Move)
	print(move is CaseMessage.Quit)
	print(quit is CaseMessage.Quit)

	# The whole union is a membership test over the declared tags.
	print(move is CaseMessage)
	print(write is CaseMessage)

	# An `and` conjunct binds for the guarded suite too.
	var flag: bool = true
	if move is CaseMessage.Move(a, b) and flag:
		print(a + b)

	# `while` binds once per iteration.
	print(first_text([quit, write, move]))

	# `assert` binds into the enclosing suite.
	assert(move is CaseMessage.Move(left, right))
	print(left * right)

	# An int-backed enum is a membership test over its declared values.
	var level: CaseLevel = CaseLevel.High
	print(level is CaseLevel)
	var not_a_level: Variant = 3
	print(not_a_level is CaseLevel)
	print(CaseLevel.Low is CaseLevel)

	# Membership is decided at runtime, so a constant agrees with a variable holding the same value.
	const DECLARED_LEVEL: int = 7
	const UNDECLARED_LEVEL: int = 3
	print(DECLARED_LEVEL is CaseLevel)
	print(UNDECLARED_LEVEL is CaseLevel)
