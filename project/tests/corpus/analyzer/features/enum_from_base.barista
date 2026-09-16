class A:
	enum Named:
		VALUE_A = 0
		VALUE_B = VALUE_A + 1
		VALUE_C = 42

class B extends A:
	var a = Named.VALUE_A
	var b = Named.VALUE_B
	var c = Named.VALUE_C

func test():
	var test_instance = B.new()
	prints("a", test_instance.a, test_instance.a == A.Named.VALUE_A)
	prints("b", test_instance.b, test_instance.b == A.Named.VALUE_B)
	prints("c", test_instance.c, test_instance.c == B.Named.VALUE_C)
