enum ForeignPatternLeft:
	None
	Value(amount: int)

enum ForeignPatternRight:
	Missing
	Amount(amount: int)

func test():
	var left: ForeignPatternLeft = ForeignPatternLeft.Value(1)
	# Both unions erase to the same runtime shape, so the mismatch has to be caught statically.
	match left:
		ForeignPatternRight.Amount(amount):
			print(amount)
		_:
			print("other")
