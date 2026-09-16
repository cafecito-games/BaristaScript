enum Enum:
	V1 = 0
	V2 = V1 + 1

func test():
	var enumAsDict: Dictionary = Enum.duplicate()
	var enumAsVariant = Enum.duplicate()
	print(Enum.has("V1"))
	print(enumAsDict.has("V1"))
	print(enumAsVariant.has("V1"))
	enumAsDict.clear()
	enumAsVariant.clear()
	print(Enum.has("V1"))
	print(enumAsDict.has("V1"))
	print(enumAsVariant.has("V1"))
