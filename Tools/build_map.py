# SPDX-License-Identifier: Apache-2.0
"""Generate materials and the club-room level (/Game/Maps/Clubroom).

Run headless after `just import-assets`: just build-map
The level is fully reproducible from this script; do not hand-edit it.
Layout: table at origin, board on the table, opponent at +Y facing -Y,
player cameras on the -Y side (cameras are derived at runtime by the game mode).
"""
import unreal

MAT_DIR = "/Game/Materials"
MAP_PATH = "/Game/Maps/Clubroom"
mel = unreal.MaterialEditingLibrary
assets = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log_warning("MAP " + msg)


ALIASES = {"volumetric_fog": ["enable_volumetric_fog"]}


def setp(obj, name, value):
    """set_editor_property that tolerates renamed properties across engine versions."""
    for n in [name] + ALIASES.get(name, []):
        try:
            obj.set_editor_property(n, value)
            return True
        except Exception:
            continue
    log(f"property {type(obj).__name__}.{name} not found; skipped")
    return False


def fresh_material(name):
    path = f"{MAT_DIR}/{name}"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    return assets.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())


def build_color_material():
    m = fresh_material("M_Color")
    color = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -400, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.8, 0.8, 0.8, 1))
    rough = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -400, 200)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", 0.45)
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)


def build_highlight_material():
    m = fresh_material("M_Highlight")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    color = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -500, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(1, 0.8, 0.2, 1))
    boost = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, 0)
    boost.set_editor_property("const_b", 2.5)
    mel.connect_material_expressions(color, "", boost, "A")
    opacity = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -500, 200)
    opacity.set_editor_property("parameter_name", "Opacity")
    opacity.set_editor_property("default_value", 0.5)
    mel.connect_material_property(boost, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)


def build_surface_material():
    """Tiled PBR surface with texture parameters (floor/walls)."""
    m = fresh_material("M_Surface")
    uv = mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -900, 0)
    tiling = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -900, 150)
    tiling.set_editor_property("parameter_name", "Tiling")
    tiling.set_editor_property("default_value", 1.0)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -700, 50)
    mel.connect_material_expressions(uv, "", mul, "A")
    mel.connect_material_expressions(tiling, "", mul, "B")

    def tex(name, y, sampler, default_path):
        t = mel.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -450, y)
        t.set_editor_property("parameter_name", name)
        t.set_editor_property("sampler_type", sampler)
        t.set_editor_property("texture", unreal.load_asset(default_path))
        mel.connect_material_expressions(mul, "", t, "UVs")
        return t

    base = tex("BaseColorTex", -200, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
               "/Game/Art/Textures/PaintedPlaster017/PaintedPlaster017_2K-JPG_Color")
    normal = tex("NormalTex", 100, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                 "/Game/Art/Textures/PaintedPlaster017/PaintedPlaster017_2K-JPG_NormalDX")
    rough = tex("RoughnessTex", 400, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
                "/Game/Art/Textures/PaintedPlaster017/PaintedPlaster017_2K-JPG_Roughness")
    tint = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -450, -400)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    tinted = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -150, -250)
    mel.connect_material_expressions(base, "RGB", tinted, "A")
    mel.connect_material_expressions(tint, "", tinted, "B")
    mel.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(rough, "R", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def build_character_material():
    """Masked, two-sided, softly self-lit material for VRoid characters.

    Interchange imports VRoid/MToon materials as translucent (glTF BLEND), which breaks
    sorting (eye whites over irises, missing bangs) and shadowing. Reparenting every
    character instance onto this opaque-masked master fixes that and gives a flatter,
    anime-like shading. Parameter names match the Interchange glTF instances.
    """
    m = fresh_material("M_Character")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("opacity_mask_clip_value", 0.4)
    # Usage flags are only auto-set by interactive edits; without them UE renders WorldGrid.
    for flag in ("used_with_skeletal_mesh", "used_with_morph_targets"):
        setp(m, flag, True)
    tex = mel.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, 0)
    tex.set_editor_property("parameter_name", "BaseColorTexture")
    # A texture parameter without a default fails to compile (-> WorldGrid checker); use engine white.
    tex.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
    factor = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -700, 250)
    factor.set_editor_property("parameter_name", "BaseColorFactor")
    factor.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    base = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -400, 0)
    mel.connect_material_expressions(tex, "RGB", base, "A")
    mel.connect_material_expressions(factor, "", base, "B")
    alpha = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -400, 200)
    mel.connect_material_expressions(tex, "A", alpha, "A")
    mel.connect_material_expressions(factor, "A", alpha, "B")
    glow = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -400, 400)
    glow.set_editor_property("parameter_name", "SelfLight")
    glow.set_editor_property("default_value", 0.22)
    emissive = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -150, 300)
    mel.connect_material_expressions(base, "", emissive, "A")
    mel.connect_material_expressions(glow, "", emissive, "B")
    rough = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -150, 500)
    rough.set_editor_property("r", 0.85)
    spec = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -150, 600)
    spec.set_editor_property("r", 0.2)
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)

    count = 0
    for path in eal.list_assets("/Game/Art/Characters", recursive=True):
        data = eal.find_asset_data(path)
        if str(data.asset_class_path.asset_name) != "MaterialInstanceConstant":
            continue
        mi = unreal.load_asset(path)
        # Re-parenting drops overrides; capture and restore the texture/colour parameters.
        textures = [(t.parameter_info.name, t.parameter_value) for t in mi.get_editor_property("texture_parameter_values")]
        vectors = [(v.parameter_info.name, v.parameter_value) for v in mi.get_editor_property("vector_parameter_values")]
        mel.set_material_instance_parent(mi, m)
        for name, value in textures:
            if value:
                mel.set_material_instance_texture_parameter_value(mi, name, value)
        for name, value in vectors:
            mel.set_material_instance_vector_parameter_value(mi, name, value)
        bo = mi.get_editor_property("base_property_overrides")
        bo.set_editor_property("override_blend_mode", False)
        bo.set_editor_property("override_two_sided", False)
        mi.set_editor_property("base_property_overrides", bo)
        mel.update_material_instance(mi)
        eal.save_loaded_asset(mi)
        count += 1
    log(f"character materials reparented: {count}")


def surface_instance(name, parent, prefix, tiling, tint=(1, 1, 1)):
    path = f"{MAT_DIR}/{name}"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    mi = assets.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    mel.set_material_instance_texture_parameter_value(mi, "BaseColorTex", unreal.load_asset(prefix + "_Color"))
    mel.set_material_instance_texture_parameter_value(mi, "NormalTex", unreal.load_asset(prefix + "_NormalDX"))
    mel.set_material_instance_texture_parameter_value(mi, "RoughnessTex", unreal.load_asset(prefix + "_Roughness"))
    mel.set_material_instance_scalar_parameter_value(mi, "Tiling", tiling)
    mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1))
    eal.save_loaded_asset(mi)
    return mi


def color_instance(name, rgb, roughness=0.5):
    path = f"{MAT_DIR}/{name}"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    mi = assets.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, unreal.load_asset(f"{MAT_DIR}/M_Color"))
    mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1))
    mel.set_material_instance_scalar_parameter_value(mi, "Roughness", roughness)
    eal.save_loaded_asset(mi)
    return mi


# --------------------------------------------------------------------------
# Level

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube")


def spawn_mesh(mesh, loc, rot=(0, 0, 0), scale=(1, 1, 1), material=None, label=None, cast_shadow=True):
    a = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*loc), unreal.Rotator(roll=rot[2], pitch=rot[0], yaw=rot[1]))
    comp = a.static_mesh_component
    comp.set_static_mesh(mesh if not isinstance(mesh, str) else unreal.load_asset(mesh))
    a.set_actor_scale3d(unreal.Vector(*scale))
    if material:
        for i in range(comp.get_num_materials()):
            comp.set_material(i, material)
    comp.set_editor_property("cast_shadow", cast_shadow)
    if label:
        a.set_actor_label(label)
    return a


def box(center, size, material, label):
    """Axis-aligned box from centre and size (cm) using the 100cm engine cube."""
    return spawn_mesh(CUBE, center, scale=(size[0] / 100, size[1] / 100, size[2] / 100), material=material, label=label)


def props_from(folder, loc, yaw=0, scale=1.0, label=None):
    """Spawn every static mesh in an imported prop folder at the same transform (multi-part props)."""
    for path in sorted(eal.list_assets(folder, recursive=True)):
        data = eal.find_asset_data(path)
        if str(data.asset_class_path.asset_name) != "StaticMesh":
            continue
        spawn_mesh(unreal.load_asset(path), loc, rot=(0, yaw, 0), scale=(scale, scale, scale),
                   label=(label or "") + "_" + str(data.asset_name))


def build_level(wood, plaster, trim, ceiling):
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if eal.does_asset_exist(MAP_PATH):
        eal.delete_asset(MAP_PATH)
    les.new_level(MAP_PATH)

    # Room shell: x in [-380, 380], y in [-360, 320], z in [0, 320]. Window opening in the +Y wall.
    X0, X1, Y0, Y1, H, T = -380, 380, -360, 320, 320, 20
    box((0, (Y0 + Y1) / 2, -5), (X1 - X0, Y1 - Y0, 10), wood, "Floor")
    box((0, (Y0 + Y1) / 2, H + 5), (X1 - X0, Y1 - Y0, 10), ceiling, "Ceiling")
    box((X0 - T / 2, (Y0 + Y1) / 2, H / 2), (T, Y1 - Y0, H), plaster, "Wall_W")
    box((X1 + T / 2, (Y0 + Y1) / 2, H / 2), (T, Y1 - Y0, H), plaster, "Wall_E")
    box((0, Y0 - T / 2, H / 2), (X1 - X0 + 2 * T, T, H), plaster, "Wall_S")
    # North wall with a 300 x 150 window opening centred at x=0, sill at 90.
    WX, WZ0, WZ1 = 150, 90, 240
    box(((X0 - T + -WX) / 2, Y1 + T / 2, H / 2), (-WX - (X0 - T), T, H), plaster, "Wall_N_L")
    box(((X1 + T + WX) / 2, Y1 + T / 2, H / 2), ((X1 + T) - WX, T, H), plaster, "Wall_N_R")
    box((0, Y1 + T / 2, WZ0 / 2), (2 * WX, T, WZ0), plaster, "Wall_N_Sill")
    box((0, Y1 + T / 2, (WZ1 + H) / 2), (2 * WX, T, H - WZ1), plaster, "Wall_N_Head")
    # Skirting boards and a window sill for a finished look.
    for i, (cx, cy, sx, sy) in enumerate([(0, Y0 + 1, X1 - X0, 2), (X0 + 1, (Y0 + Y1) / 2, 2, Y1 - Y0), (X1 - 1, (Y0 + Y1) / 2, 2, Y1 - Y0)]):
        box((cx, cy, 6), (sx, sy, 12), trim, f"Skirting_{i}")
    box((0, Y1 - 6, WZ0 - 2), (2 * WX + 20, 16, 4), trim, "WindowSill")

    # Two imported window units fill the opening (each ~149 x 35 x 126).
    for i, x in enumerate((-75, 75)):
        props_from("/Game/Art/Props/Window", (x, Y1 + T / 2, WZ0 + 12), yaw=0, scale=1.0, label=f"Window{i}")

    # Furniture.
    props_from("/Game/Art/Props/Table", (0, 0, 0), label="Table")
    props_from("/Game/Art/Props/Bookshelf", (X0 + 32, 60, 0), yaw=90, label="Bookshelf")
    props_from("/Game/Art/Props/Bookshelf", (X0 + 32, -120, 0), yaw=90, label="Bookshelf2")
    props_from("/Game/Art/Props/Chair", (230, -40, 0), yaw=200, label="Chair")
    props_from("/Game/Art/Props/Chair", (-200, 220, 0), yaw=35, label="Chair2")
    props_from("/Game/Art/Props/Plant", (WX + 45, Y1 - 30, 0), scale=3.0, label="Plant")
    props_from("/Game/Art/Props/Clock", (X1 - 2, 40, 230), yaw=-90, scale=1.0, label="Clock")
    # Side desk with lamp and books by the east wall.
    box((X1 - 45, 170, 72), (80, 140, 4), trim, "SideDesk_Top")
    for j, (dx, dy) in enumerate([(-35, -65), (35, -65), (-35, 65), (35, 65)]):
        box((X1 - 45 + dx, 170 + dy, 35), (5, 5, 70), trim, f"SideDesk_Leg{j}")
    props_from("/Game/Art/Props/Lamp", (X1 - 50, 210, 74), yaw=-110, label="Lamp")
    props_from("/Game/Art/Props/Books", (X1 - 45, 130, 74), yaw=-90, label="Books")

    # Gameplay actors (C++ classes); the game mode finds them.
    board_cls = unreal.load_class(None, "/Script/ChessSimMark.ChessBoardActor")
    opp_cls = unreal.load_class(None, "/Script/ChessSimMark.OpponentCharacter")
    actors.spawn_actor_from_class(board_cls, unreal.Vector(0, 0, 100.4), unreal.Rotator(0, 0, 0)).set_actor_label("ChessBoard")
    actors.spawn_actor_from_class(opp_cls, unreal.Vector(0, 74, 0), unreal.Rotator(yaw=-90)).set_actor_label("Opponent")

    # Lighting: late-afternoon sun through the window, sky, soft interior fill, volumetric haze.
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(roll=0, pitch=-28, yaw=-115))
    sun.set_actor_label("Sun")
    sc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    setp(sc, "intensity", 9.0)
    setp(sc, "light_color", unreal.Color(r=255, g=214, b=170, a=255))
    setp(sc, "atmosphere_sun_light", True)
    setp(sc, "volumetric_scattering_intensity", 2.5)
    setp(sc, "mobility", unreal.ComponentMobility.MOVABLE)

    sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 400))
    sky.set_actor_label("SkyLight")
    skc = sky.get_component_by_class(unreal.SkyLightComponent)
    setp(skc, "real_time_capture", True)
    setp(skc, "intensity", 1.2)
    setp(skc, "mobility", unreal.ComponentMobility.MOVABLE)
    actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0)).set_actor_label("SkyAtmosphere")
    fog = actors.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0))
    fog.set_actor_label("Fog")
    fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    setp(fc, "fog_density", 0.004)
    setp(fc, "volumetric_fog", True)
    setp(fc, "volumetric_fog_extinction_scale", 0.6)

    # Warm ceiling fill (two soft rect lights) and the desk lamp.
    for i, x in enumerate((-140, 140)):
        r = actors.spawn_actor_from_class(unreal.RectLight, unreal.Vector(x, -40, H - 4), unreal.Rotator(pitch=-90))
        r.set_actor_label(f"CeilingPanel{i}")
        rc = r.get_component_by_class(unreal.RectLightComponent)
        setp(rc, "intensity", 6.0)
        setp(rc, "intensity_units", unreal.LightUnits.CANDELAS)
        setp(rc, "source_width", 120.0)
        setp(rc, "source_height", 60.0)
        setp(rc, "attenuation_radius", 900.0)
        setp(rc, "light_color", unreal.Color(r=255, g=236, b=214, a=255))
        setp(rc, "mobility", unreal.ComponentMobility.MOVABLE)
    lamp = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(X1 - 70, 205, 135))
    lamp.set_actor_label("LampBulb")
    lc = lamp.get_component_by_class(unreal.PointLightComponent)
    setp(lc, "intensity", 4.0)
    setp(lc, "intensity_units", unreal.LightUnits.CANDELAS)
    setp(lc, "attenuation_radius", 450.0)
    setp(lc, "light_color", unreal.Color(r=255, g=190, b=120, a=255))
    setp(lc, "mobility", unreal.ComponentMobility.MOVABLE)
    # Key light on the opponent (anime-style soft key from the player side).
    key = actors.spawn_actor_from_class(unreal.SpotLight, unreal.Vector(-60, -120, 230), unreal.Rotator(pitch=-28, yaw=62))
    key.set_actor_label("CharacterKey")
    kc = key.get_component_by_class(unreal.SpotLightComponent)
    setp(kc, "intensity", 12.0)
    setp(kc, "intensity_units", unreal.LightUnits.CANDELAS)
    setp(kc, "outer_cone_angle", 35.0)
    setp(kc, "source_radius", 20.0)
    setp(kc, "attenuation_radius", 600.0)
    setp(kc, "light_color", unreal.Color(r=255, g=240, b=232, a=255))
    setp(kc, "cast_shadows", False)  # soft fill only; avoids giant lamp shadows on the wall
    setp(kc, "mobility", unreal.ComponentMobility.MOVABLE)

    # Post-process: gentle JRPG grade (bloom, warm white balance, vignette, clamped exposure).
    ppv = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 150))
    ppv.set_actor_label("PostProcess")
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")
    for prop, val in [
        ("override_bloom_intensity", True), ("bloom_intensity", 0.9),
        ("override_auto_exposure_min_brightness", True), ("auto_exposure_min_brightness", 1.0),
        ("override_auto_exposure_max_brightness", True), ("auto_exposure_max_brightness", 3.5),
        ("override_auto_exposure_bias", True), ("auto_exposure_bias", 0.6),
        ("override_vignette_intensity", True), ("vignette_intensity", 0.35),
        ("override_white_temp", True), ("white_temp", 6100.0),
        ("override_color_saturation", True), ("color_saturation", unreal.Vector4(1.08, 1.05, 1.05, 1.0)),
        ("override_film_toe", True), ("film_toe", 0.5),
        ("override_lumen_scene_lighting_quality", True), ("lumen_scene_lighting_quality", 2.0),
        ("override_lumen_final_gather_quality", True), ("lumen_final_gather_quality", 2.0),
        ("override_depth_of_field_focal_distance", True), ("depth_of_field_focal_distance", 0.0),
    ]:
        try:
            s.set_editor_property(prop, val)
        except Exception as e:  # property names drift between engine versions
            log(f"post-process property {prop} skipped: {e}")
    ppv.set_editor_property("settings", s)

    les.save_current_level()
    log(f"saved {MAP_PATH}")


def main():
    if not eal.does_directory_exist("/Game/Art/Chess"):
        log("art not imported; run `just import-assets` first")
    build_color_material()
    build_highlight_material()
    build_character_material()
    surface = build_surface_material()
    wood = surface_instance("MI_WoodFloor", surface, "/Game/Art/Textures/WoodFloor051/WoodFloor051_2K-JPG", 3.0)
    plaster = surface_instance("MI_Plaster", surface, "/Game/Art/Textures/PaintedPlaster017/PaintedPlaster017_2K-JPG", 2.0,
                               tint=(1.0, 0.94, 0.86))
    trim = color_instance("MI_Trim", (0.32, 0.18, 0.09), 0.4)
    ceiling = color_instance("MI_Ceiling", (0.85, 0.83, 0.8), 0.9)
    build_level(wood, plaster, trim, ceiling)


main()
