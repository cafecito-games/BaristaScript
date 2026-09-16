# A trait is a valid union alternative, and trait conformance decides membership in both directions:
# a conforming class satisfies a union that lists the trait, and a union of conforming classes
# satisfies the trait.
trait ZzGreeter:
	abstract func greet() -> String


class Person:
	uses ZzGreeter

	func greet() -> String:
		return "hi"


class Robot:
	uses ZzGreeter

	func greet() -> String:
		return "beep"


type GreeterOrCount = ZzGreeter | int
type AnyGreeter = Person | Robot


func take_greeter(value: ZzGreeter) -> String:
	return value.greet()


func test():
	var slot: GreeterOrCount = Person.new()
	var control: ZzGreeter = Person.new()
	var any: AnyGreeter = Robot.new()
	prints(control.greet(), take_greeter(any), slot != null)
