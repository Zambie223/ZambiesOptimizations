"""Creates or updates /ZambiesOptimizations/ZambiesOptimizationsConfiguration, the mod's SML Mod Configuration asset.

Run headless from the project root (the editor must not be open):
  UnrealEditor-Cmd.exe FactoryGame.uproject -run=pythonscript -script="<this file>" -unattended -nullrhi -nosplash

The asset is a Blueprint because SML's Mod Options widgets come from its Blueprint property classes
(the native UConfigProperty::CreateEditorWidget returns null). Keys and kinds must match the Switches table in
Source/ZambiesOptimizations/Private/ZOConfig.cpp. Every switch defaults to off.
(Adapted from Tick Profiler's Tools/create_config_asset.py.)
"""
import unreal

MOD = "ZambiesOptimizations"
ASSET_DIR = f"/{MOD}"
ASSET_NAME = "ZambiesOptimizationsConfiguration"
ASSET_PATH = f"{ASSET_DIR}/{ASSET_NAME}"
SML_PROPS = "/SML/Interface/UI/Menu/Mods/ConfigProperties"

COMMON_NOTE = (
    "Acts only on a dedicated server, which reads its own file: FactoryGame/Configs/ZambiesOptimizations.cfg\n"
    "in the server's install folder. Stop the server before editing that file. Changing this in your own game\n"
    "does not change the server you join."
)

# key, default, display name, tooltip
SETTINGS = [
    ("SkelBuildables", False, "Skip skeletal mesh updates on buildables",
     "The server stops updating the animated skeletons of machines and other buildables, which nobody sees on a\n"
     "server. Players still see machines animate. Train stations, freight platforms, truck stations and train\n"
     "vehicles are always left alone. Live switch: console variable ZOpt.SkelBuildables (until restart)."),
    ("SkelUnpossessed", False, "Skip skeletal mesh updates on offline players' bodies",
     "The server stops updating the skeletons of pawns without a controller: the bodies of players who are offline.\n"
     "A body is switched back on when its player rejoins. Live switch: console variable ZOpt.SkelUnpossessed (until restart)."),
]


def fail(message):
    unreal.log_error(f"[{MOD}] {message}")
    raise RuntimeError(message)


def load_sml_class(name):
    cls = unreal.load_class(None, f"{SML_PROPS}/{name}.{name}_C")
    if cls is None:
        fail(f"Could not load SML class {name}")
    return cls


def generated_class_of(blueprint):
    try:
        cls = unreal.BlueprintEditorLibrary.generated_class(blueprint)
        if cls is not None:
            return cls
    except Exception as error:  # library may be unavailable; fall back to loading by path
        unreal.log_warning(f"[{MOD}] BlueprintEditorLibrary.generated_class failed: {error}")
    return unreal.load_class(None, f"{ASSET_PATH}.{ASSET_NAME}_C")


def get_or_create_blueprint():
    """Updates the existing asset in place, so the asset never has to be deleted.

    Tries loading first: in the headless commandlet the asset registry has not scanned the plugin, so
    does_asset_exist() says False for an asset that exists, and create_asset() then collides with it.
    """
    blueprint = unreal.load_asset(ASSET_PATH)
    if blueprint is not None:
        return blueprint
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.ModConfiguration)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(ASSET_NAME, ASSET_DIR, unreal.Blueprint, factory)
    if blueprint is None:
        fail("create_asset returned None")
    return blueprint


def get_or_create_subobject(existing, cls, outer, name):
    """Reuses an existing instanced subobject of the right class, so re-running never clashes on the object name."""
    if existing is not None and existing.get_class() == cls:
        return existing
    return unreal.new_object(cls, outer=outer, name=name)


def main():
    blueprint = get_or_create_blueprint()
    cls = generated_class_of(blueprint)
    if cls is None:
        fail("No generated class for the Blueprint")
    cdo = unreal.get_default_object(cls)

    section_cls = load_sml_class("BP_ConfigPropertySection")
    bool_cls = load_sml_class("BP_ConfigPropertyBool")

    cdo.modify()
    root = get_or_create_subobject(cdo.get_editor_property("root_section"), section_cls, cdo, "RootSection")
    root.modify()
    existing_properties = root.get_editor_property("section_properties") or {}

    section_properties = {}
    for key, default, display_name, tooltip in SETTINGS:
        prop = get_or_create_subobject(existing_properties.get(key), bool_cls, root, key)
        prop.modify()
        prop.set_editor_property("display_name", display_name)
        prop.set_editor_property("tooltip", f"{tooltip}\n{COMMON_NOTE}")
        prop.set_editor_property("default_value", default)
        prop.set_editor_property("value", default)
        prop.set_editor_property("requires_world_reload", False)
        section_properties[key] = prop
    root.set_editor_property("section_properties", section_properties)
    # SML defaults sections to a horizontal list (with a scroll bar); the Mods menu reads top to bottom, so stack them.
    root.set_editor_property("widget_type", unreal.CP_SectionWidgetType.CPS_VERTICAL)

    config_id = unreal.ConfigId()
    config_id.set_editor_property("mod_reference", MOD)
    config_id.set_editor_property("config_category", "")
    cdo.set_editor_property("config_id", config_id)
    cdo.set_editor_property("display_name", "Zambie's Optimizations")
    cdo.set_editor_property("description", "Each optimization is off until switched on. Dedicated servers only.")
    cdo.set_editor_property("root_section", root)

    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False):
        fail("Saving the asset failed")
    unreal.log(f"[{MOD}] Saved {ASSET_PATH}")


main()
