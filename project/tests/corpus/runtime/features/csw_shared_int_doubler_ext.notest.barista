# The declaring file deliberately intended to be shared by several fixtures rather than given a
# private copy to each. It compiles once per test process; whichever fixture's `preload` runs first
# compiles it and registers `csw_shared_doubled` as a runtime witness, and every later fixture that
# preloads it gets a cached-script hit instead of a recompile. The fixture runner's per-fixture reset
# must therefore leave that already-registered witness dispatchable, or a fixture that did not happen
# to trigger the compile fails at runtime even though its code is correct.
extend int uses CswSharedIntDoubler:
	func csw_shared_doubled() -> int:
		return self * 2
