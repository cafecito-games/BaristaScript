# A `match` that leaves a case uncovered cannot terminate the function, and the error names the gap
# so the missing case does not have to be hunted down.
enum DoorState:
	Open
	Closed(locked: bool)
	Broken(reason: String)

func describe(state: DoorState) -> String:
	match state:
		DoorState.Open:
			return "open"
		DoorState.Closed(locked):
			return "closed %s" % locked

func test():
	print(describe(DoorState.Open))
