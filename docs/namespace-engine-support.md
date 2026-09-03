# Namespaces without an engine fork

An investigation of what stock Godot 4.7.2 does with a namespaced global script class, measured
against how Foundry actually implements namespaces, to decide whether BaristaScript needs an engine
patch in `~/custom-godot`.

**Conclusion: it does not.** Under GRAMMAR §0.2 **D7**, namespaced BaristaScript classes work on an
unpatched 4.7.2 engine. The engine-side namespace work in Foundry is almost entirely native-class
namespaces, which D7 deletes.

Companion documents: [`GRAMMAR.md`](GRAMMAR.md) §3.1 (D7), [`foundry-reuse-plan.md`](foundry-reuse-plan.md).

---

## 1. `ScriptServer::add_global_class` is already namespace-aware, by accident

`global_classes` is a `HashMap<StringName, GlobalScriptClass>`, and a dotted string is a perfectly
good `StringName`.

Foundry does not extend the registry with a namespace field. It registers the **fully qualified name
as the key** and splits it back apart on demand:

```cpp
// Foundry core/object/script_language.cpp:500
void ScriptServer::get_global_class_name_parts(const StringName &p_class, StringName *r_class_name,
        String *r_namespace_name) {
    const String qualified_name = p_class;
    // Namespaces are dot-joined; class identifiers cannot contain dots.
    const int namespace_separator = qualified_name.rfind(".");
    ...
}
```

and the module hands that qualified name straight to the editor scan:

```cpp
// Foundry modules/foundry_script/foundry_script.cpp:4798
return c->qualified_global_name.is_empty() ? String(c->identifier->name) : c->qualified_global_name;
```

Everything downstream — `is_global_class`, `get_global_class_path`, `get_global_class_native_base`,
the inheriters cache, `_global_script_class_cache` in `project.godot`, `ClassDB::can_instantiate`'s
script fallback, `EditorData::script_class_is_parent` — is string-keyed and namespace-blind. The
Foundry diff to `add_global_class` versus stock is `is_trait`, `is_enum`, and the builtin-globals
table. **None of it is about namespaces.**

### Stock accepts the dotted name today

Verified against `~/custom-godot/godot` at 4.7.2-stable:

- `ScriptServer::add_global_class` (`core/object/script_language.cpp:408`) validates **only** cyclic
  inheritance. No identifier check.
- Nothing in `core/`, `editor/` or `scene/` runs `is_valid_ascii_identifier()` on a global class
  name. The only identifier validation nearby is `EditorAutoloadSettings::_autoload_name_is_valid`
  (autoload names) and `GDExtension::register_extension_class` (native classes).
- `EditorFileSystem::_register_global_class_script` (`editor/file_system/editor_file_system.cpp:2571`)
  passes the language's returned name through unexamined.

### And a GDExtension can supply it

`ScriptLanguageExtension::_get_global_class_name` returns a `Dictionary` with a free-form `name` key,
and the pinned godot-cpp exposes it:

```
gen/include/godot_cpp/classes/script_language_extension.hpp:156
    virtual Dictionary _get_global_class_name(const String &p_path) const;
```

So a file declaring `namespace app.combat` + `class_name Weapon` can return `"app.combat.Weapon"`
from an unpatched extension against an unpatched engine, and it will land in the registry, persist to
`project.godot`, survive a rescan, and resolve by qualified name.

**The registry is not the blocker, and `bs_platform.h` needs no `ScriptServer` shim.**

### Confirmed at runtime

A hand-written `.godot/global_script_class_cache.cfg` carrying a dotted class alongside a flat
control, run headless on a stock-derived 4.7.2 editor build:

```
--- app.combat.Weapon ---          --- FlatWeapon ---
  can_instantiate = true             can_instantiate = true
  ClassDB.instantiate = <null>       ClassDB.instantiate = <null>
  script.new().describe() = ok       script.new().describe() = ok
  node name after set = app_combat_Weapon   node name after set = FlatWeapon
```

`can_instantiate` resolving the qualified name is the registry claim above, demonstrated. The null
from `ClassDB.instantiate` is **not** a namespace defect — it is null for the flat control too,
because global script classes instantiate through the script, not `ClassDB` (only
`can_instantiate`, `is_abstract` and `is_virtual` carry the `ScriptServer` fallback). The last row is
the node-name mangling of §3.2, reproduced.

## 2. Why Foundry's scene-unique-id patch does not apply to us

Foundry hit one *correctness* failure in core from dotted class names, fixed in #1888 and
consolidated in #1895:

> Both resource savers built a built-in resource's scene unique id from the full class name. For a
> namespaced class that prefix contains dots, which `Resource::set_scene_unique_id` rejects — but
> only after it has already overwritten the id with a random fallback.

That looks like it should bite us too. It does not, and the reason is worth writing down because it
is the load-bearing fact for this whole document.

The id prefix comes from `_resource_get_class()`, which is the **native** class:

```cpp
// scene/resources/resource_format_text.cpp:1750 (identical at core/io/resource_format_binary.cpp:2093)
static String _resource_get_class(Ref<Resource> p_resource) {
    Ref<MissingResource> missing_resource = p_resource;
    if (missing_resource.is_valid()) {
        return missing_resource->get_original_class();
    } else {
        return p_resource->get_class();
    }
}
```

and `Object::get_class()` never consults the script:

```cpp
// core/object/object.cpp:2116
const StringName &Object::get_class_name() const {
    return get_gdtype().get_name();
}
```

The script's global name travels in a **separate, quoted, dot-safe** field —
`script_class="app.combat.Weapon"` in `.tscn` (`resource_format_text.cpp:1806`) and a length-prefixed
string in binary (`resource_format_binary.cpp:2168`) — read back as an opaque string
(`recognize_script_class`).

So `_resource_get_class()` returns a dotted string **only when the native class itself is
namespaced**, which is exactly Foundry's D7 `ClassDB::class_get_namespace` feature. With D7 removed,
native class names are always flat and the prefix never contains a dot. The same reasoning clears
`EditorNode::setup_built_in_resource` (`editor/editor_node.cpp:4771`), which derives its prefix from
`p_resource->get_class()` the same way.

**No patch required.**

### The one thing still worth carrying

Independent of namespaces, `Resource::set_scene_unique_id` has a real upstream bug — it mutates
before it validates:

```cpp
// core/io/resource.cpp:172, stock 4.7.2
void Resource::set_scene_unique_id(const String &p_id) {
    bool is_valid = true;
    for (int i = 0; i < p_id.length(); i++) {
        if (!is_ascii_identifier_char(p_id[i])) {
            is_valid = false;
            scene_unique_id = Resource::generate_scene_unique_id();  // <-- clobbers on the failure path
            break;
        }
    }
    ERR_FAIL_COND_MSG(!is_valid, "The scene unique ID must contain only letters, numbers, and underscores.");
    scene_unique_id = p_id;
}
```

A rejected id (a hand-edited `.tscn`, say) gets a random replacement *and* an error, leaving the id
and the path that embeds it disagreeing. Foundry's fix is to delete that one line so a rejection
leaves the current id untouched. It is a **one-line** patcher, it is not a BaristaScript dependency,
and it is a reasonable thing to carry in `~/custom-godot` on its own merits — but it should be
proposed upstream rather than maintained as a fork delta.

## 3. What degrades: editor discovery

Nothing here produces wrong data. But calling it all "cosmetic" undersells one of them.

### 3.1 Create Node ranking — the one that actually hurts

`CreateDialog::_score_type` (`editor/gui/create_dialog.cpp:496`) penalizes a namespaced class
**twice** for a prefix the user never types:

```cpp
int pos = p_type.findn(p_search);
score = (pos > -1) ? 1.0f - w * MIN(1, 3 * pos * inverse_length) : ...;  // match must be near the start
score *= (1 - w) + w * MIN(1.0f, p_search.length() * inverse_length);    // shorter types score higher
```

Searching `Weapon`, on stock:

| Candidate | Score |
|---|---:|
| `Weapon` (flat) | **1.000** — exact-match early return |
| `WeaponRack` | 0.415 |
| `app.combat.Weapon` | **0.135** |

The class still appears in the tree, correctly nested under its base — `_add_type` groups by
inheritance, so that part is fine. But `best_match` drives auto-selection, so typing `Weapon` and
pressing Enter creates whatever flat class happens to contain the substring. Every namespaced class
is a second-class citizen in the primary discovery surface.

### 3.2 The rest

| Site | Behavior with `app.combat.Weapon` |
|---|---|
| `CreateDialog::instantiate_selected` (`:749`) does `n->set_name(type_name)` | `.` is in `invalid_node_name_characters` (`core/string/ustring.cpp:5068`) and `validate_node_name()` replaces each with `_`, so the new node is called **`app_combat_Weapon`** |
| `EditorHelp` (`editor/doc/editor_help.cpp:190`) splits `Class.member` on the last dot | A doc link misparses as class `app.combat`, member `Weapon` |
| `ScriptTextEditor::_lookup_symbol` (`:1200`) resolves the word under the cursor | One identifier, never the qualified name — BaristaScript's own `_lookup_code` has to handle this |
| FileSystem dock, inspector base types, CreateDialog favorites/recent | Show the qualified name verbatim |

Checked and found dot-tolerant: `EditorNode::_get_class_or_script_icon`, `EditorData::script_class_*`,
`EditorResourcePicker` (both the `@export` type match via `script_class_is_parent` and the "New …"
menu), `ScriptCreateDialog::_create_new` base resolution, `SceneTreeDock`'s
`derive_script_globals_by_name` (which writes `extends app.combat.Weapon` — valid BaristaScript),
and every `ClassDB` script-fallback path (`can_instantiate`, `is_abstract`, `is_virtual`).

### 3.3 The patcher

§3.1 and the node naming in §3.2 are fixed by
`~/custom-godot` patcher **`namespaced-script-class-editor-affordances`**:

1. `_score_type` scores the simple name (`substr(rfind_char('.') + 1)`) whenever the search still
   matches it, so a namespaced class competes on equal terms. Flat names are untouched —
   `rfind_char()` reports -1 and the whole name is taken.
2. `instantiate_selected` names the new node from the simple name.

That lifts `app.combat.Weapon` from 0.135 to 0.648 on a `Weapon` search (a flat exact `Weapon` still
wins at 1.000, which is correct), and gives the node its real name.

This is a **convenience** patch, not a dependency: BaristaScript still runs correctly on stock 4.7.2,
so the extension stays shippable to anyone not on the custom build — they just get a worse Create
Node dialog. `EditorHelp`'s link splitting is left for M7 with the rest of editor integration.

## 4. What is D7 and therefore out of scope

The bulk of Foundry's namespace work in the engine is **native class namespaces** — putting
`HTTPServer` under `foundry.http.server` in `ClassDB` itself. That is #1855, #1861, #1863, #1865,
#1867, #1885, #1888, #1891, #1895, #1896: a namespace identity model in `ClassDB`, canonical-key
registration, `class_get_namespace`, `class_get_by_qualified_name`, scene node-type resolution
through a resolver, the Create Node dialog grouping, the class reference, and the scene-unique-id fix
in §2.

`GRAMMAR.md` §0.2 **D7 removes all of it.** BaristaScript namespaces govern BaristaScript
declarations only; native classes stay flat global names. That is why the engine-side cost of
namespaces here is zero rather than a fork.

The one place D7 costs something: `fs_editor.cpp:1475`'s namespace index populates from *both*
`ScriptServer::get_global_class_list()` and `ClassDB::class_get_namespace()`. The BaristaScript port
keeps only the first loop.

## 5. Non-instantiable declarations: `enum_name`, `tuple_name`, `trait_name`

A namespace makes a second question urgent. BaristaScript has global names that are **not scripts you
can attach to a node** — an `enum_name` or `tuple_name` file declares a type, and a `trait_name` file
declares a contract. Stock `ScriptServer::add_global_class` carries only `is_abstract` and `is_tool`,
so at first glance a GDExtension has no way to say "this global class is an enum" and the editor
would happily offer it in the Create Node dialog.

It does not, and Foundry's extra flags are not the reason.

### Foundry's `is_trait` / `is_enum` are persistence, not behavior

Engine-side, the two flags Foundry added to the registry appear in exactly one place:

```cpp
// Foundry editor/editor_data.cpp:1398
d["is_trait"] = ScriptServer::is_global_class_trait(class_name);
d["is_enum"]  = ScriptServer::is_global_class_enum(class_name);
```

That is `script_class_save_global_classes()` — round-tripping the flags through
`global_script_class_cache.cfg` so the module can read them back. No engine behavior branches on
either flag. They are not what keeps an enum out of the editor.

### The two gates that actually do the work

Both are in stock, and both are reachable from a GDExtension.

**Inheritance.** `CreateDialog::_should_hide_type` calls
`EditorData::script_class_is_parent(p_type, "Node")` (`editor/editor_data.cpp:1038`). With no base
the loop walks `app.combat.DamageKind` → `""` → neither a `ClassDB` class nor a global class →
`false`, so the type is hidden. It terminates cleanly: registering an empty-base entry loaded with no
error, no crash, and the rest of the class list intact.

Foundry's own `_get_global_class_name` already reports it this way:

```cpp
if (r_base_type && (c->is_enum_file || c->is_tuple_file)) {
    // An `enum_name` or `tuple_name` file declares a type, not a script: it has no base class.
    *r_base_type = String();
}
```

**Abstractness.** The last line of `_should_hide_type` loads the script and hides it when
`scr->is_abstract()`; `ClassDB::can_instantiate` consults the same thing. Measured on a stock-derived
4.7.2 build:

| cache `is_abstract` | script itself | `can_instantiate` |
|---|---|---|
| `true` | plain | **true** — the cached flag is ignored |
| `true` | `@abstract` | **false** — this is the gate |

So the operative signal is `Script::is_abstract()`, and `ScriptExtension::_is_abstract` is exposed to
godot-cpp (§3 of the reuse plan already confirms it).

### What BaristaScript should do

Report an **empty base** and **`_is_abstract() = true`** for `enum_name`, `tuple_name` and
`trait_name` files. They then never appear in the Create Node dialog, Change Type, or the scene-root
picker, while keeping FileSystem dock icons, documentation pages, and autoload name-collision
detection. No engine change.

Two loose ends this leaves, both on the extension's side rather than the engine's:

- `ScriptCreateDialog`'s "Inherits" field will still offer these names, and the editor will let a user
  attach an enum file to a node as a script. BaristaScript's instance creation must refuse that with
  a clear error rather than relying on the editor to prevent it.
- Stock's `script_class_save_global_classes()` writes a **fixed** set of keys, so a GDExtension cannot
  round-trip an `is_enum` flag through Godot's cache the way Foundry does. See the export constraint
  in `foundry-reuse-plan.md` §5.6.

## 6. Two things the module owns instead of the engine

1. **`get_global_class_name_parts`.** Foundry put it on `ScriptServer` because two engine files
   (`editor_file_system.cpp:497` and `:2478`) needed it. BaristaScript's engine consumers need
   nothing of the sort, so it becomes a static helper in `bs_platform.h` — pure `String` work, zero
   engine surface.
2. **`get_reserved_global_names()`.** Foundry added this `ScriptLanguage` virtual so autoload
   validation rejects a name shadowing a language-owned global (e.g. a namespace root).
   `ScriptLanguageExtension` has no equivalent, and stock
   `EditorAutoloadSettings::_autoload_name_is_valid` checks only `get_reserved_words()` — which **is**
   exposed to extensions. Listing a namespace root in `_get_reserved_words()` gets most of the way
   there without a patch. A dedicated patcher is optional and can wait for a concrete collision.

## 7. Recommendation

- **No engine change is needed for namespaces to be correct.** The premise that
  `ScriptServer::add_global_class` must learn about namespaces does not survive contact with
  Foundry's own implementation — Foundry never taught it, it just used qualified names as keys.
- **Carry `namespaced-script-class-editor-affordances`** (§3.3) for the Create Node dialog. It is an
  editor-affordance patch, ~15 lines, inert for projects without namespaced script classes.
- **Keep BaristaScript itself buildable against stock 4.7.2.** The patched editor is strictly better,
  never required. Do not let anything in the extension depend on it.
- **Register `enum_name` / `tuple_name` / `trait_name` files with an empty base and
  `_is_abstract() = true`** (§5). That is what keeps a non-instantiable declaration out of the Create
  Node dialog — not Foundry's `is_trait`/`is_enum` registry flags, which are engine-side persistence
  only.
- **Optionally carry the one-line `set_scene_unique_id` validate-before-mutate fix** (§2) as a
  separate patcher, on its own merits, and send it upstream.
- **Revisit at M7** whether `EditorHelp`'s link splitting justifies a third patcher.

### Feedback for the reuse plan

`foundry-reuse-plan.md` §3 lists `core/object/script_language.h` under "known non-mappings" only for
the *trait* virtuals (D5). That holds. Worth stating explicitly that the global class registry maps
cleanly and namespaces cost the extension **nothing** at the seam — one fewer risk on M1.
