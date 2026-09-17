# A static frame carries two independent things: the class that *declares* the running function, and
# the receiver the frame was entered with. An unqualified call nested inside a static function resolves
# against the declaring class, so a subclass that shadows the helper never captures a call made from
# its base's body — whether the outer static is reached through the subclass handle or through an
# instance of it. `Self`, which rides on the receiver instead of the class slot, still binds to the leaf.
@warning_ignore_start("static_called_on_instance")


class Base:
	static func helper() -> String:
		return "base helper"

	static func run() -> String:
		# Unqualified: resolved against the declaring class, `Base`.
		return helper()

	static func spawn() -> Self:
		return Self.new()


class Sub extends Base:
	static func helper() -> String:
		return "sub helper"


func test() -> void:
	# The shadow really exists, so a "base helper" result below is dispatch, not a missing override.
	print("direct shadow: %s" % [Sub.helper()])

	print("class handle nested call: %s" % [Sub.run()])

	var sub_instance := Sub.new()
	print("instance receiver nested call: %s" % [sub_instance.run()])

	# The receiver still drives `Self`, so both forms construct the leaf rather than the declaring class.
	print("class handle Self is Sub: %s" % [Sub.spawn() is Sub])
	print("instance receiver Self is Sub: %s" % [sub_instance.spawn() is Sub])
