# A guard can fail, so a guarded branch never proves its case is covered. A `match` made only of
# guarded branches still needs a fallthrough return.
enum Reading:
	Low(value: int)
	High(value: int)

func describe(reading: Reading) -> String:
	match reading:
		Reading.Low(value) when value < 0:
			return "negative"
		Reading.High(value) when value > 100:
			return "over"

func test():
	print(describe(Reading.Low(-1)))
