namespace top_level_enum_demo.consumer
import top_level_enum_demo.status

var imported_value: TopLevelNamespacedEnum = TopLevelNamespacedEnum.STARTED
var qualified_value: top_level_enum_demo.status.TopLevelNamespacedEnum = (
	top_level_enum_demo.status.TopLevelNamespacedEnum.STOPPED
)

func accepts_imported(value: TopLevelNamespacedEnum) -> void:
	match value:
		TopLevelNamespacedEnum.STARTED:
			pass
		TopLevelNamespacedEnum.STOPPED:
			pass

func accepts_qualified(value: top_level_enum_demo.status.TopLevelNamespacedEnum) -> void:
	match value:
		top_level_enum_demo.status.TopLevelNamespacedEnum.STARTED:
			pass
		top_level_enum_demo.status.TopLevelNamespacedEnum.STOPPED:
			pass
