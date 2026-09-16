# Handling every declared member of a plain enum looks exhaustive but is not: the integer carrier can
# also hold undeclared values. A void function has no missing-return error to surface that, so the
# open-enum warning is the only diagnostic, and it must not claim every member is handled.
enum Level:
	LOW = 0
	HIGH = 1


func handle(value: Level) -> void:
	match value:
		Level.LOW:
			print("low")
		Level.HIGH:
			print("high")


func test():
	handle(Level.LOW)
	handle(Level.HIGH)
