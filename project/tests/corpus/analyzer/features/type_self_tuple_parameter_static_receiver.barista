# A call through a class handle has an exact receiver, so `Self` is substituted to that class and a
# nested `Self` becomes an ordinary argument check -- an instance of the named class is admitted
# without needing to be the receiver of anything.
class Receiver:
	static func take_pair(pair: (int, Self)) -> void:
		print("pair ", pair.0)


class Sub:
	extends Receiver


func test() -> void:
	Receiver.take_pair((1, Receiver.new()))
	Sub.take_pair((2, Sub.new()))
