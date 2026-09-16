const Alias = TopLevelStandaloneEnum

var current: TopLevelStandaloneEnum = TopLevelStandaloneEnum.GREEN
var alias_member: int = Alias.YELLOW

func accept_enum(value: TopLevelStandaloneEnum) -> void:
	match value:
		TopLevelStandaloneEnum.RED:
			pass
		TopLevelStandaloneEnum.GREEN:
			pass
		_:
			pass

func check_member_value() -> void:
	var member_value: int = TopLevelStandaloneEnum.GREEN
	var local: TopLevelStandaloneEnum = TopLevelStandaloneEnum.RED
	var names: Array = TopLevelStandaloneEnum.keys()
	accept_enum(local)
	accept_enum(member_value)
	print(names)
