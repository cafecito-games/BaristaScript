# A tuple in the script's own namespace wins over an unnamespaced global of the same short name, both
# for the type annotation and for the constructor call.
namespace tuples.precedence

func test():
	var point: ShadowedTuple = ShadowedTuple(3, 4)
	print(point)
	print(point.x + point.y)
