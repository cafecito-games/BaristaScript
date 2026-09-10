var property:
	get:
		return 1
		print("unreachable getter")
	set(value):
		return
		print("unreachable setter")


func test():
	print(property)
