enum LogLevel:
	INFO = 1
	WARNING = 2
	ERROR = 3
	ALIAS = WARNING
	COMPOSITE = clamp(4, 0, 8)

enum:
	LEFT = 0
	RIGHT = 1 << 1

class EnumFixtureHost:
	enum Level:
		IDLE = 0
		ACTIVE = 1

func test():
	print(LogLevel.INFO)
	print(LogLevel.WARNING)
	print(LogLevel.ERROR)
	print(LogLevel.ALIAS)
	print(LogLevel.COMPOSITE)
	print(LEFT)
	print(RIGHT)
	print(EnumFixtureHost.Level.ACTIVE)
