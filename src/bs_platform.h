/**************************************************************************/
/*  bs_platform.h                                                         */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

/**
 * The single compatibility seam between BaristaScript's ported Foundry frontend and godot-cpp.
 *
 * Foundry's frontend is engine-module code: it spells its dependencies as `core/` includes and
 * relies on Godot's core headers being on the include path. A GDExtension has godot-cpp instead,
 * which mirrors most of those types under different paths and, in a few places, does not mirror
 * them at all. This header is the only place that difference is written down. A ported file
 * replaces its whole `core/` include block with `#include "bs_platform.h"` and changes nothing
 * else; no ported file may include a godot-cpp header directly for a type mapped here.
 *
 * The mapping is audited, not asserted. `src/bs_platform_manifest.json` records every upstream
 * upstream dependency of the port set with its resolution, and `tests/audit_platform_seam.py`
 * checks that record against the godot-cpp header set the build will actually generate. What the
 * seam refuses to do matters more than what it does: it never aliases an upstream type to a
 * near-miss godot-cpp type, and it never expands a missing macro to nothing. A gap is a compile
 * error here, not a behaviour change three milestones later.
 */

// core/templates/hash_map.h
#include <godot_cpp/templates/hash_map.hpp>
// core/templates/hash_set.h
#include <godot_cpp/templates/hash_set.hpp>
// core/templates/list.h
#include <godot_cpp/templates/list.hpp>
// core/templates/vector.h
#include <godot_cpp/templates/vector.hpp>

// core/variant/callable.h -- analyzer-only utility identity, with no runtime registration.
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/callable_custom.hpp>

// core/error/error_macros.h
#include <godot_cpp/core/error_macros.hpp>
// core/math/math_defs.h -- core reaches Math:: transitively through core/math/math_funcs.h.
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/math_defs.hpp>

// core/object/class_db.h -- godot-cpp's `ClassDB` forwards the introspection methods onto the
// `ClassDBSingleton` binding (godot-cpp/include/godot_cpp/core/class_db.hpp:214), so the core
// spelling works unchanged. The one method it does not forward is `is_class_exposed()`, which it
// does not need to: the ClassDB an extension talks to is built from the exposed API.
#include <godot_cpp/core/class_db.hpp>
// core/object/object.h -- only PropertyInfo is used; godot-cpp keeps it under core/, not classes/.
#include <godot_cpp/core/property_info.hpp>
// core/object/ref_counted.h -- core declares RefCounted and Ref<T> together.
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
// core/object/script_language.h -- core declares Script and ScriptLanguage together.
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/script_language.hpp>
// core/io/resource.h
#include <godot_cpp/classes/resource.hpp>
// core/io/resource_loader.h -- a singleton in godot-cpp, static members in core.
#include <godot_cpp/classes/resource_loader.hpp>
// core/io/resource_uid.h -- a singleton in godot-cpp, static members in core.
#include <godot_cpp/classes/resource_uid.hpp>
// core/config/project_settings.h
#include <godot_cpp/classes/project_settings.hpp>

// core/config/engine.h
#include <godot_cpp/classes/engine.hpp>
// scene/main/node.h -- deliberately omitted. The build profile does not generate Node; ClassDB
// ancestry checks (SNAME("Node")) do not need the typed Node binding. See the manifest.
// core/object/method_bind.h
#include <godot_cpp/core/method_bind.hpp>
// core/variant/type_info.h
#include <godot_cpp/core/type_info.hpp>
// scene/main/multiplayer_api.h -- the `@rpc` annotation reads the RPCMode enumerators from
// MultiplayerAPI and the TransferMode ones from MultiplayerPeer, which core declares in a header
// this one includes and godot-cpp splits into its own.
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
// servers/text/text_server.h -- the confusable-identifier check M1 guarded out, reinstated by the
// warning registry through the public interface rather than the engine-internal TS macro. core
// reaches the primary interface through TS; godot-cpp goes through TextServerManager, so the seam
// includes both headers. See the decision recorded in src/bs_warning.h.
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/classes/text_server_manager.hpp>

// Backing for the parse cache's on-disk store, also not a mapping of any upstream dependency:
// upstream fs_cache is in-memory only -- its only file access is reading script sources
// (fs_cache.cpp:407 at the pinned revision) -- so the store's atomic rename
// (DirAccess::rename_absolute) and FileAccess's byte-array return type (PackedByteArray) are
// BaristaScript additions. Recorded in the manifest's seam_support_headers, not as entries,
// because there is no upstream include site to map them to.
#include <godot_cpp/classes/dir_access.hpp>

// Cohesive private implementation groups. These files are reachable only through this umbrella;
// the manifest audit owns their allowlist and rejects direct frontend includes of them.
#include "bs_platform_names.h"
#include "bs_platform_serialization.h"
#include "bs_platform_variant.h"

/**
 * Ported files are written against Godot's global names. godot-cpp puts everything in `godot`, so
 * the seam opens it once here rather than making every ported file carry a `using` line the
 * upstream file does not have. This is deliberate: the seam exists so that the diff against
 * Foundry stays readable.
 */
using namespace godot;

namespace barista_script {
// Definition lives in bs_core_constants.h; metadata comes from the pinned engine API producer.
class BSCoreConstants;
using CoreConstants = BSCoreConstants;
} // namespace barista_script

/**
 * `docs/foundry-reuse-plan.md` section 3 predicted that godot-cpp spells the error macros
 * differently and that the seam would need a shim. It does not: every macro the port set uses is
 * spelled identically. A wrapper would therefore be a rename-free no-op, which the seam's contract
 * forbids. What the seam does instead is refuse to compile when one of them is missing, so a
 * godot-cpp bump that drops or renames a macro fails here rather than silently expanding to
 * nothing at a call site. The list is kept in step with `required_macros` in the manifest by
 * `tests/audit_platform_seam.py`.
 */
#if !defined(ERR_CONTINUE) || !defined(ERR_CONTINUE_MSG)
#error "bs_platform.h: godot-cpp no longer defines the ERR_CONTINUE macros the ported frontend uses."
#endif
#if !defined(ERR_FAIL_COND) || !defined(ERR_FAIL_COND_MSG) || !defined(ERR_FAIL_COND_V) || !defined(ERR_FAIL_COND_V_MSG)
#error "bs_platform.h: godot-cpp no longer defines the ERR_FAIL_COND macros the ported frontend uses."
#endif
#if !defined(ERR_FAIL_INDEX) || !defined(ERR_FAIL_INDEX_V) || !defined(ERR_FAIL_INDEX_V_MSG)
#error "bs_platform.h: godot-cpp no longer defines the ERR_FAIL_INDEX macros the ported frontend uses."
#endif
#if !defined(ERR_FAIL_NULL) || !defined(ERR_FAIL_NULL_MSG) || !defined(ERR_FAIL_NULL_V)
#error "bs_platform.h: godot-cpp no longer defines the ERR_FAIL_NULL macros the ported frontend uses."
#endif
#if !defined(ERR_FAIL_V) || !defined(ERR_FAIL_V_MSG) || !defined(ERR_PRINT) || !defined(DEV_ASSERT)
#error "bs_platform.h: godot-cpp no longer defines the error macros the ported frontend uses."
#endif

/**
 * D1 gives BaristaScript one integer type, so Foundry's `NumericType` and the numeric tower built
 * on it are deleted rather than ported. Nothing here defines the type; the identifier is redirected
 * to one that does not exist, so any surviving reference -- including a pointer or a reference,
 * which a forward declaration would have allowed -- is a compile error that names the decision.
 *
 * A stub would compile. That is exactly the failure this is here to prevent: it would let the tower
 * grow back one call site at a time, and the corpus would not notice.
 *
 * Verified by hand, not by a test, because the proof is the absence of a translation unit:
 *
 *     #include "bs_platform.h"
 *     NumericType example;   // error: unknown type name
 *                            // 'BS_NumericType_was_deleted_by_D1_see_docs_GRAMMAR_md'
 *     NumericType *pointer;  // the same error; the redirect leaves nothing to point at
 */
#define NumericType BS_NumericType_was_deleted_by_D1_see_docs_GRAMMAR_md
