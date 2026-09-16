# A nullable plain-enum subject can also hold `null`, which the unhandled list names alongside any
# unhandled members without presenting it as a declared member of the enum.
enum Level:
	LOW = 0
	HIGH = 1


func handle(value: Level?) -> void:
	match value:
		Level.LOW:
			print("low")
		Level.HIGH:
			print("high")


func test():
	handle(Level.LOW)
	handle(null)
