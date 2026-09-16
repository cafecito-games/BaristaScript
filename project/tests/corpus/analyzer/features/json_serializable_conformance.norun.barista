# A class conforms to the builtin `JsonSerializable` trait with no import: the trait, `JsonNode`,
# and `JsonResult` all ship with the engine as global types.
@warning_ignore("mixed_namespace_directory")
class_name FixtureJsonItem extends RefCounted
uses JsonSerializable

var name: String
var count: int

func to_json() -> JsonNode:
	var entries: Dictionary[String, JsonNode] = {}
	entries["name"] = JsonNode.Str(name)
	entries["count"] = JsonNode.Int(count)
	return JsonNode.Object(entries)

static func from_json(node: JsonNode) -> JsonResult[FixtureJsonItem]:
	match node:
		JsonNode.Object(var entries):
			var item := FixtureJsonItem.new()
			if not entries.has("name"):
				return JsonResult[FixtureJsonItem].fail("Missing field.", "$.name")
			match entries["name"]:
				JsonNode.Str(var name_value):
					item.name = name_value
				_:
					return JsonResult[FixtureJsonItem].fail("Expected a string.", "$.name")
			if not entries.has("count"):
				return JsonResult[FixtureJsonItem].fail("Missing field.", "$.count")
			match entries["count"]:
				JsonNode.Int(var count_value):
					item.count = count_value
				_:
					return JsonResult[FixtureJsonItem].fail("Expected an integer.", "$.count")
			return JsonResult[FixtureJsonItem].ok(item)
		_:
			return JsonResult[FixtureJsonItem].fail("Expected an object.", "$")
