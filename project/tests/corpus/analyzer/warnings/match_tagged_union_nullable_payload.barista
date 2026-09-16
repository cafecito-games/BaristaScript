enum Placement:
	Unset
	At(spot: (int, int)?)

func test():
	var placement: Placement = Placement.At(null)
	# The nested tuple pattern rejects a null payload, so it does not cover the case.
	match placement:
		Placement.Unset:
			print("unset")
		Placement.At((var x, var y)):
			prints(x, y)
	print("ok")
