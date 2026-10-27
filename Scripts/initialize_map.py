"""Create the first empty test map without overwriting an existing map."""
import unreal

map_path = "/Game/Maps/L_Herding"
if unreal.EditorAssetLibrary.does_asset_exist(map_path):
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not levels.load_level(map_path):
        raise RuntimeError("Failed to load " + map_path)
    unreal.log("NIAGARA_HERD_SETUP: Existing map preserved: " + map_path)
else:
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not levels.new_level(map_path):
        raise RuntimeError("Failed to create " + map_path)
    if not levels.save_current_level():
        raise RuntimeError("Failed to save " + map_path)
    unreal.log("NIAGARA_HERD_SETUP: Created empty map: " + map_path)
