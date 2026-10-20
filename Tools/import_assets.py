# SPDX-License-Identifier: Apache-2.0
"""Import third-party placeholder art from private/assets into /Game/Art.

Run headless: just import-assets
Idempotent: existing assets are replaced. Sources/licences: CREDITS.md.
"""
import os
import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SRC = os.path.join(ROOT, "private", "assets")

IMPORTS = [
    # (source file relative to private/assets, destination package path)
    ("chess/chess_set/chess_set.glb", "/Game/Art/Chess"),
    ("characters/lia/lia_morphs.glb", "/Game/Art/Characters/Lia"),
    ("characters/kayn_song/kayn_song_morphs.glb", "/Game/Art/Characters/Kayn"),
    ("furniture/round_wooden_table_01/round_wooden_table_01.glb", "/Game/Art/Props/Table"),
    ("furniture/SchoolChair_01/SchoolChair_01.glb", "/Game/Art/Props/Chair"),
    ("furniture/wooden_bookshelf_worn/wooden_bookshelf_worn.glb", "/Game/Art/Props/Bookshelf"),
    ("props/book_encyclopedia_set_01/book_encyclopedia_set_01.glb", "/Game/Art/Props/Books"),
    ("props/desk_lamp_arm_01/desk_lamp_arm_01.glb", "/Game/Art/Props/Lamp"),
    ("props/potted_plant_04/potted_plant_04.glb", "/Game/Art/Props/Plant"),
    ("props/wall_clock/wall_clock.glb", "/Game/Art/Props/Clock"),
    ("props/weathered_wooden_window/weathered_wooden_window.glb", "/Game/Art/Props/Window"),
    ("props/window_curtain/window_curtain.glb", "/Game/Art/Props/Curtain"),
    ("hdri/art_studio/art_studio_2k.hdr", "/Game/Art/HDRI"),
    ("hdri/lythwood_lounge/lythwood_lounge_4k.hdr", "/Game/Art/HDRI"),
]

TEXTURES = {
    "/Game/Art/Textures/WoodFloor051": [
        "textures/WoodFloor051/WoodFloor051_2K-JPG_Color.jpg",
        "textures/WoodFloor051/WoodFloor051_2K-JPG_NormalDX.jpg",
        "textures/WoodFloor051/WoodFloor051_2K-JPG_Roughness.jpg",
        "textures/WoodFloor051/WoodFloor051_2K-JPG_AmbientOcclusion.jpg",
    ],
    "/Game/Art/Textures/PaintedPlaster017": [
        "textures/PaintedPlaster017/PaintedPlaster017_2K-JPG_Color.jpg",
        "textures/PaintedPlaster017/PaintedPlaster017_2K-JPG_NormalDX.jpg",
        "textures/PaintedPlaster017/PaintedPlaster017_2K-JPG_Roughness.jpg",
    ],
}


def task(filename, dest):
    t = unreal.AssetImportTask()
    t.filename = filename
    t.destination_path = dest
    t.automated = True
    t.replace_existing = True
    t.save = True
    return t


def main():
    tasks = []
    for rel, dest in IMPORTS:
        path = os.path.join(SRC, rel)
        if not os.path.exists(path):
            unreal.log_warning(f"IMPORT missing source {path} (run the asset fetch first)")
            continue
        tasks.append(task(path, dest))
    for dest, files in TEXTURES.items():
        for rel in files:
            path = os.path.join(SRC, rel)
            if os.path.exists(path):
                tasks.append(task(path, dest))
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    for t in tasks:
        unreal.log_warning(f"IMPORT {os.path.basename(t.filename)} -> {len(t.imported_object_paths)} objects")

    # Normal maps must not be sRGB and need the normal compression setting.
    for asset in unreal.EditorAssetLibrary.list_assets("/Game/Art/Textures", recursive=True):
        tex = unreal.EditorAssetLibrary.load_asset(asset)
        if isinstance(tex, unreal.Texture2D):
            name = tex.get_name()
            if "Normal" in name:
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
                tex.set_editor_property("srgb", False)
            elif "Roughness" in name or "AmbientOcclusion" in name:
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
                tex.set_editor_property("srgb", False)
            unreal.EditorAssetLibrary.save_loaded_asset(tex)

    for asset in sorted(unreal.EditorAssetLibrary.list_assets("/Game/Art", recursive=True)):
        obj = unreal.EditorAssetLibrary.find_asset_data(asset)
        unreal.log_warning(f"ASSET {obj.asset_class_path.asset_name} {asset}")


main()
