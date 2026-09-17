# https://github.com/cafecito-games/godot/issues/60
async func immediate() -> String:
	return "awaited explicit async"

func test():
	var value := await immediate()
	print(value)
