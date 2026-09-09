# Resolving the enum's own name inside its body does not make a bare case name a constructor: a
# payload case is still only spelled on the enum name.
const EnumFile = preload("./enum_name_bare_payload_case_in_own_body.notest.fs")

func test():
	print(EnumFile.End)
