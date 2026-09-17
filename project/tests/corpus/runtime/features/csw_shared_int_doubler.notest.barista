# Companion trait for the shared-declaring-file conformance fixtures. Multiple consumer fixtures
# (`conformance_builtin_witness_shared_declaring_file_*`) preload the same conformance file below and
# must all be able to dispatch its witness in the same process run, regardless of which one actually
# triggers the compile.
trait_name CswSharedIntDoubler

abstract func csw_shared_doubled() -> int
