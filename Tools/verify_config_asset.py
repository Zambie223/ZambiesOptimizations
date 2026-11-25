"""Loads /ZambiesOptimizations/ZambiesOptimizationsConfiguration in a fresh editor process and prints what was saved.

Run headless from the project root (the editor must not be open):
  UnrealEditor-Cmd.exe FactoryGame.uproject -run=pythonscript -script="<this file>" -unattended -nullrhi -nosplash
Output goes to Saved/Logs/FactoryGame.log, not stdout. Ends with "VERIFY OK" or an error.
(Adapted from Tick Profiler's Tools/verify_config_asset.py.)
"""
import unreal

MOD = "ZambiesOptimizations"
# key -> (property class name, default); must match create_config_asset.py and the Switches table in ZOConfig.cpp.
EXPECTED = {
    "SkelBuildables": ("BP_ConfigPropertyBool_C", False),
    "SkelUnpossessed": ("BP_ConfigPropertyBool_C", False),
}

cls = unreal.load_class(None, f"/{MOD}/ZambiesOptimizationsConfiguration.ZambiesOptimizationsConfiguration_C")
if cls is None:
    unreal.log_error(f"[{MOD}] VERIFY: class not found")
    raise RuntimeError("class not found")
cdo = unreal.get_default_object(cls)
config_id = cdo.get_editor_property("config_id")
unreal.log(f"[{MOD}] VERIFY config_id = {config_id.get_editor_property('mod_reference')!r} / {config_id.get_editor_property('config_category')!r}")
root = cdo.get_editor_property("root_section")
if root is None:
    unreal.log_error(f"[{MOD}] VERIFY: root_section is None")
    raise RuntimeError("root_section missing")
properties = root.get_editor_property("section_properties")
problems = []
for key, (class_name, default) in EXPECTED.items():
    prop = properties.get(key)
    if prop is None:
        problems.append(f"{key} missing")
        continue
    actual_class = prop.get_class().get_name()
    actual_default = prop.get_editor_property("default_value")
    unreal.log(f"[{MOD}] VERIFY {key} -> {actual_class} default={actual_default} name='{prop.get_editor_property('display_name')}'")
    if actual_class != class_name:
        problems.append(f"{key} is {actual_class}, expected {class_name}")
    if actual_default != default:
        problems.append(f"{key} default {actual_default}, expected {default}")
extra = [key for key in properties.keys() if key not in EXPECTED]
if extra:
    problems.append(f"unexpected keys {extra}")
if config_id.get_editor_property("mod_reference") != MOD:
    problems.append("wrong mod_reference")
if problems:
    unreal.log_error(f"[{MOD}] VERIFY FAILED: {problems}")
    raise RuntimeError("verify failed")
unreal.log(f"[{MOD}] VERIFY OK ({len(properties)} settings)")
