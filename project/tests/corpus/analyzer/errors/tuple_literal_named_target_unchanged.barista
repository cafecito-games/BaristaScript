# A named tuple type is nominal, so an unnamed literal can never satisfy it. Typing the literal's
# elements toward a type it is going to be rejected against would only confuse the report, so the
# literal is left alone.
tuple Point(x: float, y: float)


func test():
	var point: Point = (1.0, 2.0)
	print(point)
