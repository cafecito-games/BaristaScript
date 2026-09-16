enum TrafficSignal:
	Stop
	Go(speed: int)
	Turn(degrees: int)

func test():
	var command: TrafficSignal = TrafficSignal.Go(3)
	match command:
		TrafficSignal.Stop:
			print("stop")
		TrafficSignal.Go(speed):
			print(speed)
	print("ok")
