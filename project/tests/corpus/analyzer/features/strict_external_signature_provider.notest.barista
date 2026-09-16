signal event(value: int)

var callback: Callable[[int], bool] = accepts_int
var typed_event: Signal[[int]] = event

func accepts_int(value: int) -> bool:
	return true

func get_callback() -> Callable[[int], bool]:
	return accepts_int

func get_event() -> Signal[[int]]:
	return event
