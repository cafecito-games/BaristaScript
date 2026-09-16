# An unhandled declared member is still worth naming, in declaration order, but adding the missing
# patterns is not the fix: only an unguarded catch-all closes a plain enum's integer carrier.
enum Level:
	LOW = 0
	MEDIUM = 1
	HIGH = 2


func handle(value: Level) -> void:
	match value:
		Level.MEDIUM:
			print("medium")


func test():
	handle(Level.MEDIUM)
	handle(Level.LOW)
