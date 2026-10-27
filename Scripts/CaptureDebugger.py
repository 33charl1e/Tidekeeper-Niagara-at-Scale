"""Capture PIE debug HUD in a dedicated editor process; closes that process afterward."""
import time
import unreal
from pathlib import Path

_start=time.monotonic()
_phase=0
_handle=None

def _tick(dt):
    global _phase,_handle
    age=time.monotonic()-_start
    if _phase==0 and age>2:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
        _phase=1
    elif _phase==1 and age>6:
        worlds=unreal.EditorLevelLibrary.get_pie_worlds(False)
        if not worlds: return
        world=worlds[0]
        for command in ['herd.Mode 1','herd.Count 100000','ShowFlag.Particles 1','ShowFlag.OnScreenDebug 1',
                        'fx.Niagara.Debug.Hud Enabled=1 OverviewEnabled=1 OverviewLocation=250,380 SystemFilter=NS_Herd* OverviewMode=0']:
            unreal.SystemLibrary.execute_console_command(world,command)
        unreal.log('HERD_DEBUGGER_PIE_ENABLED')
        _phase=2
    elif _phase==2 and age>12:
        worlds=unreal.EditorLevelLibrary.get_pie_worlds(False)
        if worlds:
            shot=(Path(unreal.Paths.project_saved_dir())/'Screenshots'/'Niagara-Debugger-GPU.png').resolve().as_posix()
            unreal.SystemLibrary.execute_console_command(worlds[0],f'shot SHOWUI filename="{shot}" -nosuffix')
        _phase=3
    elif _phase==3 and age>15:
        worlds=unreal.EditorLevelLibrary.get_pie_worlds(False)
        if worlds:
            for command in ['herd.Mode 0','herd.Count 500','fx.Niagara.Debug.Hud OverviewMode=2']:
                unreal.SystemLibrary.execute_console_command(worlds[0],command)
        _phase=4
    elif _phase==4 and age>20:
        worlds=unreal.EditorLevelLibrary.get_pie_worlds(False)
        if worlds:
            shot=(Path(unreal.Paths.project_saved_dir())/'Screenshots'/'Niagara-Debugger-CPU.png').resolve().as_posix()
            unreal.SystemLibrary.execute_console_command(worlds[0],f'shot SHOWUI filename="{shot}" -nosuffix')
        _phase=5
    elif _phase==5 and age>28:
        unreal.unregister_slate_post_tick_callback(_handle)
        unreal.SystemLibrary.quit_editor()

_handle=unreal.register_slate_post_tick_callback(_tick)
