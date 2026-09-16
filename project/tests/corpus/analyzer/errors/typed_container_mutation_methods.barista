func test():
	var values: Array[int] = [1]
	var texts: Array[String] = ["bad"]
	values.append("bad")
	values.insert(0, "bad")
	values.set(0, "bad")
	values.assign(texts)

	var scores: Dictionary[String, int] = { "one": 1 }
	var text_scores: Dictionary[String, String] = { "one": "bad" }
	var int_scores: Dictionary[int, int] = { 1: 1 }
	scores.set(1, 1)
	scores.set("one", "bad")
	scores.assign(text_scores)
	scores.assign(int_scores)
	scores.merge(text_scores)
	scores.merge(int_scores)
