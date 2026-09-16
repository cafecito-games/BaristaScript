abstract class AbstractClass:
	abstract func some_func()

class ImplementedClass extends AbstractClass:
	func some_func():
		pass

abstract class AbstractClassAgain extends ImplementedClass:
	abstract func some_func()

class Test1:
	abstract func some_func()

class Test2 extends AbstractClass:
	pass

class Test3 extends AbstractClassAgain:
	pass

class Test4 extends AbstractClass:
	func some_func():
		super()

	func other_func():
		super.some_func()

abstract class A:
	# An abstract function cannot have a body.
	abstract func abstract_bodyful():
		pass

func holding_some_invalid_lambda(invalid_default_arg = func():):
	var some_invalid_lambda = (func():)

func test():
	pass
