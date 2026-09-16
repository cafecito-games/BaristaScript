# A dictionary value type may also name the union being declared.
enum Config:
	Scalar(value: int)
	Section(entries: Dictionary[String, Config])

func total(node: Config) -> int:
	match node:
		Config.Scalar(var value):
			return value
		Config.Section(var entries):
			var sum := 0
			for key: String in entries:
				sum += total(entries[key])
			return sum
	return 0

func test():
	print(total(Config.Section({"a": Config.Scalar(1), "b": Config.Scalar(2)})))
