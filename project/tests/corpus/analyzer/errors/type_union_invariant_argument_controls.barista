# The rejections that already worked keep working: a non-union callable in the same invariant slot,
# two unions whose alternatives differ in declared numeric width, and ordinary non-invariant union
# compatibility at a plain parameter. All three run through the comparison the deeper union identity
# changes, so they pin that it was extended rather than replaced.
class Keeper[T]:
	var held: T


func take_callable(k: Keeper[Callable[[int], void]]) -> void:
	print("callable ", k)


func take_width(k: Keeper[int | String]) -> void:
	print("width ", k)


func take_plain(v: Callable[[int], void] | String) -> void:
	print("plain ", v)


func relay_callable(k: Keeper[Callable[[String], void]]) -> void:
	take_callable(k)


func relay_width(k: Keeper[long | String]) -> void:
	take_width(k)


func relay_plain(v: Callable[[String], void] | String) -> void:
	take_plain(v)


func test() -> void:
	print("skipped")
