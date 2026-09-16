# A shorthand pattern covers its case, so the uncovered ones are named exactly as they are for the
# qualified spelling.
enum TrafficState:
	Stop
	Go(speed: int)
	Turn(degrees: int)

func test():
	var command: TrafficState = .Go(3)
	match command:
		.Stop:
			print("stop")
		.Go(speed):
			print(speed)
	print("ok")
