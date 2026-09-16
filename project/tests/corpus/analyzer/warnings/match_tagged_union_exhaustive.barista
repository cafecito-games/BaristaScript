enum Command:
	Halt
	Step(count: int)
	Say(text: String)

func test():
	var command: Command = Command.Step(2)
	# Every case is covered: a payload-less case as a value, and case patterns whose payload
	# patterns accept every value of their case.
	match command:
		Command.Halt:
			print("halt")
		Command.Step(count):
			print(count)
		Command.Say(_):
			print("say")
	print("ok")
