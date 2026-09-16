const A := 1
enum:
	B = 0
enum NamedEnum:
	C = 0

class Parent:
	const D := 2
	enum:
		E = 0
	enum NamedEnum2:
		F = 0

class Child extends Parent:
	enum TestEnum:
		A = 0
		B = A + 1
		C = B + 1
		D = C + 1
		E = D + 1
		F = E + 1
		Node = F + 1
		Object = Node + 1
		Child = Object + 1
		Parent = Child + 1

func test():
	print(A, B, NamedEnum.C, Parent.D, Parent.E, Parent.NamedEnum2.F)
	print(Child.TestEnum.A, Child.TestEnum.B, Child.TestEnum.C, Child.TestEnum.D, Child.TestEnum.E, Child.TestEnum.F)
	print(Child.TestEnum.Node, Child.TestEnum.Object, Child.TestEnum.Child, Child.TestEnum.Parent)
