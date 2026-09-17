func test_continue_for():
	var ref := RefCounted.new()
	for _i in range(2):
		print(ref.get_reference_count())
		var _temp := ref
		continue


func test_continue_while():
	var ref := RefCounted.new()
	var i := 0
	while i < 2:
		print(ref.get_reference_count())
		var _temp := ref
		i += 1
		continue


func test():
	test_continue_for()
	test_continue_while()
