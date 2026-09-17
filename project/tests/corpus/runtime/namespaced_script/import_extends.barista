import fs_ns_extends.runtime

extends FSNsExtendsRuntimeBase

func test() -> void:
	label = "imported"
	print(describe())

	var qualified := FSNsExtendsRuntimeQualified.new()
	qualified.label = "qualified"
	print(qualified.describe_twice())
