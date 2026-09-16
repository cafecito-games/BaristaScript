enum Reading:
	Empty
	Level(value: int)

func test():
	var reading: Reading = Reading.Level(0)
	# A literal payload pattern can fail, so it does not cover the case.
	match reading:
		Reading.Empty:
			print("empty")
		Reading.Level(0):
			print("zero")
	print("ok")
