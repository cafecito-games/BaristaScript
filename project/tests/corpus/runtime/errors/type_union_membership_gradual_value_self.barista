# The same answer when every alternative names `Self`: admitted at analysis on the promise that the
# store checks it, and the store then finds the value is none of the alternatives.
class Receiver:
	var counter = 5

	func drive() -> void:
		var soft = counter
		var link: (int, Self) | (String, Self) = soft
		print("initialized ", link)


func test() -> void:
	Receiver.new().drive()
