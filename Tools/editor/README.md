# Tools/editor

Editor Python scripts, run headless with the `pythonscript` commandlet (PythonScriptPlugin is enabled for Editor targets in `Velox.uproject`).

- `create_bootstrap_content.py` - creates `L_Empty`, Enhanced Input assets and the default data assets. Idempotent. See `docs/content-bootstrap.md`.

Notes on the 5.8 Python API (learned the hard way):
- `UPROPERTY(EditDefaultsOnly)` members of *structs* cannot be set with `set_editor_property`; pass them to the struct constructor kwargs instead (`unreal.VeloxInputAction(input_action=..., input_tag=...)`).
- `unreal.GameplayTag` has no constructor args; use `t = unreal.GameplayTag(); t.import_text('(TagName="InputTag.Move")')`.
- `unreal.Key` likewise: `k = unreal.Key(); k.set_editor_property("key_name", "SpaceBar")`.
- `InputMappingContext.map_key()` returns the mapping by value; attach modifiers by editing `default_key_mappings.mappings` and writing the struct back.
