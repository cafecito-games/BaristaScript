const preloaded: FoundryScript = preload("fs_to_preload.notest.fs")

func test():
	var preloaded_instance: preloaded = preloaded.new()
	print(preloaded_instance.something())
