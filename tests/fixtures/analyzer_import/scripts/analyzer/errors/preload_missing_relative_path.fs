extends RefCounted

const Missing = preload("./preload_missing_relative_target.notest.fs")

func test():
	print(Missing)
