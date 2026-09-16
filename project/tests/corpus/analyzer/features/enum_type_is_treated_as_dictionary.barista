enum MyEnum:
	ZERO = 0
	ONE = ZERO + 1
	TWO = ONE + 1

func test():
	for key in MyEnum.keys():
		prints(key, MyEnum[key])

	# https://github.com/godotengine/godot/issues/55491
	for key in MyEnum:
		prints(key, MyEnum[key])
