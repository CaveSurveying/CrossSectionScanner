# SPDX-License-Identifier: CERN-OHL-S-2.0
"""Build the CrossSectionScanner case as a parametric FreeCAD document.

Headless (model, STLs and checks):  freecadcmd hardware/case/build_case.py
In the GUI (adds colours):          freecad hardware/case/build_case.py
Colours without a window:           CASE_BUILD_QUIT=1 QT_QPA_PLATFORM=offscreen freecad hardware/case/build_case.py

Every size is a named cell in the Params spreadsheet. Change it there and
recompute (Ctrl+R). Axes: x forward from the tail, y to the left, z up.
Once the .FCStd is edited by hand it becomes the source; don't rebuild over it.
"""
import math
import os
import sys

import FreeCAD as App
import MeshPart
import Part
from FreeCAD import Rotation, Vector

try:
    HERE = os.path.dirname(os.path.abspath(__file__))
except NameError:
    HERE = os.path.join(os.getcwd(), "hardware", "case")
FCSTD = os.path.join(HERE, "scanner_case.FCStd")
STL_DIR = os.path.join(HERE, "stl")

# name, value or formula, note
PARAMS = [
    ("wall", 2.4, "shell wall"),
    ("bodyH", 44, "body height, handle and head"),
    ("lidT", 4, "lid thickness"),
    ("handleL", 70, ""),
    ("handleW", 36, ""),
    ("handleR", 8, "handle edge radius"),
    ("headL", 96, "head length, rear bay included"),
    ("headW", 56, ""),
    ("headR", 3, "head edge radius"),
    ("frontX", "=handleL + headL", "front face of the body"),
    ("axisZ", "=bodyH / 2", "lidar axis height"),
    # lidar plate and cup
    ("plateS", 62, "plate and cup, square"),
    ("plateT", 5, ""),
    ("cupH", 21, "cup height from the plate face, below the optical head"),
    ("cupWall", 2.2, ""),
    ("plateScrewY1", 12, "M3 plate screws, lower pair"),
    ("plateScrewZ1", 10, ""),
    ("plateScrewY2", 20, "M3 plate screws, upper pair, below the board"),
    ("plateScrewZ2", 21, ""),
    # lidar cable, out of the left side of the base, back into a PG7 gland
    ("cableD", 5, ""),
    ("cableExitH", 5, "cable centre above the lidar mounting face, measure"),
    ("bootL", 10, "strain relief boot, measure"),
    ("bootD", 8, "measure"),
    ("cableBendR", 10, ""),
    ("glandDist", 35, "cable exit to the gland face"),
    ("cableX", "=frontX + plateT + cableExitH", ""),
    ("glandX", "=cableX - glandDist", "gland face, front of the side pod"),
    ("glandY", "=lidarS / 2 + bootL + cableBendR", "gland axis"),
    ("glandHoleD", 12.7, "PG7, typical sizes, measure"),
    ("glandOutD", 15, ""),
    ("glandOutL", 18, ""),
    ("glandNutD", 17.3, "lock nut across corners"),
    ("glandNutT", 4, ""),
    ("podL", 28, "sealed side pod behind the gland face"),
    ("podY", "=glandY + glandNutD / 2 + wall + 0.5", "outer side of the pod and hood"),
    ("hoodEnd", "=cableX + bootD / 2 + wall + 1", "front of the hood over the boot and bend"),
    # RPLIDAR C1, datasheet
    ("lidarS", 55.6, "base square"),
    ("lidarBaseH", 23.1, ""),
    ("lidarHeadH", 18.2, ""),
    ("lidarHeadD", 50, "optical head diameter, measure"),
    ("lidarHole", 43, "M2.5 square pitch"),
    ("laserH", 29.8, "scan plane above the base"),
    # E32R24P board
    ("boardL", 74.3, ""),
    ("boardW", 42.9, ""),
    ("boardT", 13.6, "screen face to back of plugs, measure"),
    ("boardX0", "=frontX - wall - boardL", "board against the front wall"),
    ("boardZ0", "=bodyH - wall - boardT", "board up against the top wall"),
    ("winL", 52, "screen window, measure"),
    ("winW", 39, ""),
    ("winFront", 3.6, "window to the board's top end, measure"),
    # cells and clamping
    ("cellD", 18.5, "18650"),
    ("cellL", 69, "with tabs"),
    ("cellPitch", 20, ""),
    ("cellX0", "=boardX0 + 0.7", ""),
    ("cellZc", 16.5, "cell axis height"),
    ("frameT", 3, "board frame, cradles the cells"),
    ("frameZ0", "=boardZ0 - frameT", ""),
    ("strapT", 2, "cell straps"),
    ("strapW", 6, ""),
    ("strapZ0", "=cellZc - cellD / 2 - strapT + 0.25", ""),
    ("clampX1", 100, "strap bosses, on the side walls"),
    ("clampX2", 141, ""),
    ("clampY", 23, ""),
    ("clampBossD", 6, ""),
    ("clampInsD", 3.6, "M2.5 heat-set insert"),
    # lid fixing
    ("lidBossD", 6, ""),
    ("lidInsD", 4.0, "M3 heat-set insert"),
    ("lidScrewD", 3.4, ""),
    ("lidX1", 36, "handle lid screws"),
    ("lidX2", 66, ""),
    ("lidHandleY", 12, ""),
    ("lidX3", 124, "head lid screws"),
    ("lidX4", 158, ""),
    ("lidHeadY", 24.5, ""),
    # IMU (GY-85) upright on standoffs from the tail end wall
    ("imuL", 22, "upright"),
    ("imuW", 17, "across"),
    ("imuT", 1.6, ""),
    ("imuStand", 3, "standoff length"),
    ("imuX0", "=wall + imuStand", ""),
    ("imuZ0", 15, "above the lanyard block"),
    ("imuPostD", 5, ""),
    # controls and fittings
    ("btnX", 58, "thumb button on the handle top"),
    ("btnD", 12, "panel switch hole"),
    ("ringOD", 17, "guard ring"),
    ("ringID", 14, ""),
    ("ringH", 2, ""),
    ("usbX", 81, "USB-C screw-cap socket, right side of the bay"),
    ("usbZ", 16, ""),
    ("usbD", 12.2, "panel hole, measure the socket"),
    ("usbBodyD", 16, "nut and body inside"),
    ("usbBodyL", 20, ""),
    ("boostX0", 73.5, "5 V boost in the bay, choose a module this size"),
    ("boostL", 15, ""),
    ("boostW", 14, ""),
    ("boostT", 6, ""),
    ("lanyardD", 4, ""),
    ("lanyardBlock", 8, "solid block in the tail for the lanyard hole"),
]

doc = None


def P(name):
    return f"Params.{name}"


def ex(v):
    return f"({v})"


def setx(obj, prop, expr):
    obj.setExpression(prop, str(expr))


def place(obj, x, y, z):
    setx(obj, ".Placement.Base.x", x)
    setx(obj, ".Placement.Base.y", y)
    setx(obj, ".Placement.Base.z", z)


def box(name, x, y, z, L, W, H):
    o = doc.addObject("Part::Box", name)
    setx(o, "Length", L)
    setx(o, "Width", W)
    setx(o, "Height", H)
    place(o, x, y, z)
    return o


def cyl(name, axis, x, y, z, d, h):
    """Cylinder from (x, y, z) along +axis."""
    o = doc.addObject("Part::Cylinder", name)
    setx(o, "Radius", f"{ex(d)} / 2")
    setx(o, "Height", h)
    if axis == "x":
        o.Placement.Rotation = Rotation(Vector(0, 1, 0), 90)
    elif axis == "y":
        o.Placement.Rotation = Rotation(Vector(1, 0, 0), -90)
    place(o, x, y, z)
    return o


def fuse(name, objs):
    o = doc.addObject("Part::MultiFuse", name)
    o.Shapes = objs
    o.Refine = True
    return o


def common(name, objs):
    o = doc.addObject("Part::MultiCommon", name)
    o.Shapes = objs
    return o


def cut(name, base, tool):
    o = doc.addObject("Part::Cut", name)
    o.Base = base
    o.Tool = tool
    o.Refine = True
    return o


def rrect(name, x0, L, W, z0, H, r, yc=0):
    """Prism along x, centred on y = yc, with its four long edges rounded."""
    W, H, r, yc = ex(W), ex(H), ex(r), ex(yc)
    parts = [
        box(f"{name}_a", x0, f"{yc} - ({W} / 2 - {r})", z0, L, f"{W} - 2 * {r}", H),
        box(f"{name}_b", x0, f"{yc} - {W} / 2", f"{ex(z0)} + {r}", L, W, f"{H} - 2 * {r}"),
    ]
    for i, (sy, top) in enumerate([(1, 0), (-1, 0), (1, 1), (-1, 1)]):
        y = f"{yc} + {sy} * ({W} / 2 - {r})"
        z = f"{ex(z0)} + {H} - {r}" if top else f"{ex(z0)} + {r}"
        parts.append(cyl(f"{name}_e{i}", "x", x0, y, z, f"2 * {r}", L))
    return fuse(name, parts)


def make_params():
    sheet = doc.addObject("Spreadsheet::Sheet", "Params")
    sheet.set("A1", "name")
    sheet.set("B1", "mm")
    sheet.set("C1", "note")
    for row, (name, value, note) in enumerate(PARAMS, start=2):
        sheet.set(f"A{row}", name)
        sheet.set(f"B{row}", str(value))
        sheet.setAlias(f"B{row}", name)
        if note:
            sheet.set(f"C{row}", note)
    sheet.setColumnWidth("C", 360)
    return sheet


def body_outer(prefix):
    handle = rrect(f"{prefix}Handle", 0, P("handleL"), P("handleW"), 0, P("bodyH"), P("handleR"))
    head = rrect(f"{prefix}Head", P("handleL"), P("headL"), P("headW"), 0, P("bodyH"), P("headR"))
    # sealed side pod with the gland, and the hood over the cable bend in front of it
    pod_y0 = f"{P('headW')} / 2 - {P('headR')} - 1"
    pod = rrect(f"{prefix}Pod", f"{P('glandX')} - {P('podL')}", f"{P('frontX')} - {P('glandX')} + {P('podL')}",
                f"{P('podY')} - {ex(pod_y0)}", 0, P("bodyH"), P("headR"), f"({P('podY')} + {ex(pod_y0)}) / 2")
    hood = rrect(f"{prefix}Hood", P("frontX"), f"{P('hoodEnd')} - {P('frontX')}",
                 f"{P('podY')} - {P('plateS')} / 2", 0, P("bodyH"), P("headR"),
                 f"({P('podY')} + {P('plateS')} / 2) / 2")
    return fuse(f"{prefix}Outer", [handle, head, pod, hood])


def lid_boss_points():
    """(x, y) of the lid screws."""
    pts = []
    for x, y in [("lidX1", "lidHandleY"), ("lidX2", "lidHandleY"), ("lidX3", "lidHeadY"), ("lidX4", "lidHeadY")]:
        pts += [(P(x), P(y)), (P(x), f"-{P(y)}")]
    pts.append((f"{P('glandX')} - {P('podL')} + 6", f"{P('podY')} - 6"))  # side pod
    return pts


def clamp_points():
    pts = []
    for x in ("clampX1", "clampX2"):
        pts += [(P(x), P("clampY")), (P(x), f"-{P('clampY')}")]
    return pts


def plate_screw_points():
    pts = []
    for y, z in [("plateScrewY1", "plateScrewZ1"), ("plateScrewY2", "plateScrewZ2")]:
        pts += [(P(y), P(z)), (f"-{P(y)}", P(z))]
    return pts


def imu_post_points():
    """(y, z) of the IMU standoffs, diagonal corners; measure the real holes."""
    return [(f"{P('imuW')} / 2 - 3", f"{P('imuZ0')} + 3"),
            (f"-({P('imuW')} / 2 - 3)", f"{P('imuZ0')} + {P('imuL')} - 3")]


def make_main():
    w = P("wall")
    outer = body_outer("M")
    slab = box("MSlab", -1, -100, P("lidT"), f"{P('hoodEnd')} + 2", 200, f"{P('bodyH')} + 1")
    shell0 = common("MShell0", [outer, slab])
    in_handle = rrect("MInHandle", w, f"{P('handleL')} + 1", f"{P('handleW')} - 2 * {w}",
                      w, f"{P('bodyH')} - 2 * {w}", f"{P('handleR')} - {w}")
    in_head = rrect("MInHead", f"{P('handleL')} + {w}", f"{P('headL')} - 2 * {w}",
                    f"{P('headW')} - 2 * {w}", w, f"{P('bodyH')} - 2 * {w}", 0.6)
    top_in = f"{P('bodyH')} - {w}"
    pod_x0 = f"{P('glandX')} - {P('podL')}"
    in_pod = box("MInPod", f"{pod_x0} + {w}", f"{P('headW')} / 2 - {w} - 0.5", w,
                 f"{P('podL')} - 2 * {w}", f"{P('podY')} - {w} - ({P('headW')} / 2 - {w} - 0.5)", f"{top_in} - {w}")
    in_hood1 = box("MInHood1", P("glandX"), f"{P('headW')} / 2", f"{P('lidT')} - 1",
                   f"{P('frontX')} - {P('glandX')} + 0.01", f"{P('podY')} - {w} - {P('headW')} / 2",
                   f"{top_in} - {P('lidT')} + 1")
    in_hood2 = box("MInHood2", f"{P('frontX')} - 0.01", f"{P('plateS')} / 2 - 0.5", f"{P('lidT')} - 1",
                   f"{P('hoodEnd')} - {w} - {P('frontX')} + 0.01", f"{P('podY')} - {w} - {P('plateS')} / 2 + 0.5",
                   f"{top_in} - {P('lidT')} + 1")
    shell = cut("MShell", shell0, fuse("MInner", [in_handle, in_head, in_pod, in_hood1, in_hood2]))

    adds = []
    for i, (x, y) in enumerate(lid_boss_points()):
        adds.append(cyl(f"MLidBoss{i}", "z", x, y, P("lidT"), P("lidBossD"), f"{top_in} - {P('lidT')} + 0.5"))
    clamp_z0 = f"{P('strapZ0')} + {P('strapT')}"
    for i, (x, y) in enumerate(clamp_points()):
        adds.append(cyl(f"MClampBoss{i}", "z", x, y, clamp_z0, P("clampBossD"), f"{P('frameZ0')} - {ex(clamp_z0)}"))
    for i, (y, z) in enumerate(imu_post_points()):
        adds.append(cyl(f"MImuPost{i}", "x", f"{w} - 0.5", y, z, P("imuPostD"), f"{P('imuStand')} + 0.5"))
    adds.append(box("MLanyardBlock", f"{w} - 0.5", -20, P("lidT"), f"{P('lanyardBlock')} + 0.5", 40, P("lanyardBlock")))
    body = common("MBody", [fuse("MAdds", [shell] + adds), shell0])

    ring = cut("MRing", cyl("MRingO", "z", P("btnX"), 0, f"{P('bodyH')} - 0.5", P("ringOD"), f"{P('ringH')} + 0.5"),
               cyl("MRingI", "z", P("btnX"), 0, f"{P('bodyH')} - 1", P("ringID"), f"{P('ringH')} + 2"))
    body = fuse("MBodyRing", [body, ring])

    holes = [
        box("MWindow", f"{P('boardX0')} + {P('boardL')} - {P('winFront')} - {P('winL')}", f"-{P('winW')} / 2",
            f"{top_in} - 1", P("winL"), P("winW"), f"{w} + 2"),
        cyl("MBtnHole", "z", P("btnX"), 0, f"{top_in} - 1", P("btnD"), f"{w} + {P('ringH')} + 3"),
        cyl("MUsbHole", "y", P("usbX"), f"-{P('headW')} / 2 - 1", P("usbZ"), P("usbD"), f"{w} + 2"),
        cyl("MGlandHole", "x", f"{P('glandX')} - {w} - 1", P("glandY"), P("axisZ"), P("glandHoleD"), f"{w} + 2"),
        cyl("MLanyardHole", "y", f"{w} + {P('lanyardBlock')} / 2", -30, f"{P('lidT')} + {P('lanyardBlock')} / 2",
            P("lanyardD"), 60),
    ]
    for i, (y, z) in enumerate(plate_screw_points()):
        holes.append(cyl(f"MPlateScrew{i}", "x", f"{P('frontX')} - {w} - 1", y, z, P("lidScrewD"), f"{w} + 2"))
    for i, (x, y) in enumerate(lid_boss_points()):
        holes.append(cyl(f"MLidIns{i}", "z", x, y, f"{P('lidT')} - 1", P("lidInsD"), 6))
    for i, (x, y) in enumerate(clamp_points()):
        holes.append(cyl(f"MClampIns{i}", "z", x, y, f"{ex(clamp_z0)} - 1", P("clampInsD"), 5))
    for i, (y, z) in enumerate(imu_post_points()):  # blind, 1 mm short of the outside
        holes.append(cyl(f"MImuHole{i}", "x", 1, y, z, 2.2, f"{P('imuX0')} - 1 + 1"))
    return cut("MainPart", body, fuse("MHoles", holes)), outer


def make_lid():
    outer = body_outer("L")
    lid0 = common("LLid0", [outer, box("LSlab", -1, -100, -1, f"{P('hoodEnd')} + 2", 200, f"{P('lidT')} + 1")])
    holes = []
    for i, (x, y) in enumerate(lid_boss_points()):
        holes.append(cyl(f"LScrew{i}", "z", x, y, -1, P("lidScrewD"), f"{P('lidT')} + 2"))
        holes.append(cyl(f"LHead{i}", "z", x, y, -1, 6, 3))
    return cut("Lid", lid0, fuse("LHoles", holes))


def make_plate():
    fx, s, t = P("frontX"), P("plateS"), P("plateT")
    face = f"{fx} + {t}"
    plate = box("PPlate", fx, f"-{s} / 2", f"{P('axisZ')} - {s} / 2", t, s, s)
    cup_o = box("PCupO", face, f"-{s} / 2", f"{P('axisZ')} - {s} / 2", P("cupH"), s, s)
    inner = f"{s} - 2 * {P('cupWall')}"
    cup_i = box("PCupI", f"{face} - 0.01", f"-{ex(inner)} / 2", f"{P('axisZ')} - {ex(inner)} / 2",
                f"{P('cupH')} + 1", inner, inner)
    body = cut("PBody", fuse("PSolid", [plate, cup_o]), cup_i)
    holes = [  # slot in the left cup wall for the cable boot
        box("PCableSlot", f"{face} - 0.01", f"{s} / 2 - {P('cupWall')} - 0.5", f"{P('axisZ')} - {P('bootD')} / 2 - 0.5",
            f"{P('cableExitH')} + {P('bootD')} / 2 + 0.5", f"{P('cupWall')} + 1", f"{P('bootD')} + 1"),
    ]
    h = f"{P('lidarHole')} / 2"
    for i, (sy, sz) in enumerate([(1, 1), (1, -1), (-1, 1), (-1, -1)]):
        y, z = f"{sy} * {h}", f"{P('axisZ')} + {sz} * {h}"
        holes.append(cyl(f"PLidarHole{i}", "x", f"{fx} - 1", y, z, 2.8, f"{t} + 2"))
        holes.append(cyl(f"PLidarHead{i}", "x", f"{fx} - 1", y, z, 5, 3.5))
    for i, (y, z) in enumerate(plate_screw_points()):
        holes.append(cyl(f"PInsert{i}", "x", f"{fx} - 1", y, z, P("lidInsD"), 5))
    return cut("LidarPlate", body, fuse("PHoles", holes))


def make_frame():
    w = P("wall")
    half = f"{P('headW')} / 2 - {w} - 0.3"
    frame = box("FBox", P("boardX0"), f"-({half})", P("frameZ0"),
                f"{P('frontX')} - {w} - 0.3 - {P('boardX0')}", f"2 * ({half})", P("frameT"))
    tools = []
    for i, sy in enumerate((1, -1)):
        tools.append(cyl(f"FCradle{i}", "x", f"{P('boardX0')} - 1", f"{sy} * {P('cellPitch')} / 2", P("cellZc"),
                         f"{P('cellD')} + 0.5", f"{P('boardL')} + 2"))
    for i, (x, y) in enumerate(lid_boss_points()[4:]):
        tools.append(cyl(f"FNotch{i}", "z", x, y, 0, f"{P('lidBossD')} + 0.8", P("bodyH")))
    return cut("BoardFrame", frame, fuse("FTools", tools))


def make_strap(n, x):
    half = f"{P('clampY')} + 2.3"
    strap = box(f"S{n}Box", f"{x} - {P('strapW')} / 2", f"-({half})", P("strapZ0"), P("strapW"), f"2 * ({half})",
                P("strapT"))
    tools = [cyl(f"S{n}Cradle{i}", "x", f"{x} - 10", f"{sy} * {P('cellPitch')} / 2", P("cellZc"),
                 f"{P('cellD')} + 0.5", 20) for i, sy in enumerate((1, -1))]
    tools += [cyl(f"S{n}Hole{i}", "z", x, f"{sy} * {P('clampY')}", f"{P('strapZ0')} - 1", 2.8, f"{P('strapT')} + 2")
              for i, sy in enumerate((1, -1))]
    return cut(f"CellStrap{n}", strap, fuse(f"S{n}Tools", tools))


def make_dummies():
    fx, t = P("frontX"), P("plateT")
    base_x = f"{fx} + {t}"
    s = P("lidarS")
    lidar = fuse("Lidar", [
        box("DLidarBase", base_x, f"-{s} / 2", f"{P('axisZ')} - {s} / 2", P("lidarBaseH"), s, s),
        cyl("DLidarHead", "x", f"{base_x} + {P('lidarBaseH')}", 0, P("axisZ"), P("lidarHeadD"), P("lidarHeadH")),
    ])
    board = box("Board", P("boardX0"), f"-{P('boardW')} / 2", P("boardZ0"), P("boardL"), P("boardW"), P("boardT"))
    cells = [cyl(f"Cell{i}", "x", P("cellX0"), f"{sy} * {P('cellPitch')} / 2", P("cellZc"), P("cellD"), P("cellL"))
             for i, sy in enumerate((1, -1))]
    imu = box("IMU", P("imuX0"), f"-{P('imuW')} / 2", P("imuZ0"), P("imuT"), P("imuW"), P("imuL"))
    boost = box("Boost", P("boostX0"), f"{P('headW')} / 2 - {P('wall')} - {P('boostW')} - 1",
                f"{P('bodyH')} - {P('wall')} - {P('boostT')}", P("boostL"), P("boostW"), P("boostT"))
    usb = cyl("UsbSocket", "y", P("usbX"), f"-{P('headW')} / 2 + {P('wall')}", P("usbZ"), P("usbBodyD"),
              P("usbBodyL"))
    switch = cyl("Switch", "z", P("btnX"), 0, f"{P('bodyH')} - {P('wall')} - 18", f"{P('btnD')} - 0.2", 20)
    w, gx = P("wall"), P("glandX")
    gland = fuse("Gland", [
        cyl("DGlandOut", "x", gx, P("glandY"), P("axisZ"), P("glandOutD"), P("glandOutL")),
        cyl("DGlandThread", "x", f"{gx} - {w}", P("glandY"), P("axisZ"), f"{P('glandHoleD')} - 0.2", w),
        cyl("DGlandNut", "x", f"{gx} - {w} - {P('glandNutT')}", P("glandY"), P("axisZ"), P("glandNutD"),
            P("glandNutT")),
    ])
    boot_end = f"{P('lidarS')} / 2 + {P('bootL')}"
    bend = doc.addObject("Part::Torus", "DCableBend")
    setx(bend, "Radius1", P("cableBendR"))
    setx(bend, "Radius2", f"{P('cableD')} / 2")
    bend.Angle3 = 90
    place(bend, f"{P('cableX')} - {P('cableBendR')}", boot_end, P("axisZ"))
    cable = fuse("Cable", [
        cyl("DCableBoot", "y", P("cableX"), f"{P('lidarS')} / 2", P("axisZ"), P("bootD"), P("bootL")),
        bend,
        cyl("DCableRun", "x", f"{gx} - 12", P("glandY"), P("axisZ"), P("cableD"),
            f"{P('cableX')} - {P('cableBendR')} - {gx} + 12"),
    ])
    return [lidar, board, *cells, imu, boost, usb, switch, gland, cable]


def group(name, objs, visible=True):
    g = doc.addObject("App::DocumentObjectGroup", name)
    for o in objs:
        g.addObject(o)
    return g


# print orientation: (rotation, about point) for each printed part
def print_shape(obj):
    s = obj.Shape.copy()
    if obj.Name == "MainPart":
        s.rotate(Vector(0, 0, 0), Vector(1, 0, 0), 180)  # screen face on the bed
    elif obj.Name == "LidarPlate":
        s.rotate(Vector(0, 0, 0), Vector(0, 1, 0), -90)  # plate on the bed
    b = s.BoundBox
    s.translate(Vector(-b.XMin, -b.YMin, -b.ZMin))
    return s


def scan_plane(sheet):
    """Thin disc in the scan plane, for the clearance check only."""
    x = sheet.frontX + sheet.plateT + sheet.laserH - 0.1
    return Part.makeCylinder(200, 0.2, Vector(x, 0, sheet.axisZ), Vector(1, 0, 0))


def checks(printed, dummies, sheet):
    out = []
    ok = True
    for o in printed:
        b = print_shape(o).BoundBox
        fits = max(b.XLength, b.YLength) <= 195 and b.ZLength <= 195
        ok &= fits
        out.append(f"{'ok  ' if fits else 'FAIL'} {o.Name}: {b.XLength:.1f} x {b.YLength:.1f} x {b.ZLength:.1f} as printed, "
                   f"{o.Shape.Volume / 1000:.1f} cm3, valid {o.Shape.isValid()}")
    meant = {frozenset(("Cable", "Gland"))}  # the cable runs through the gland
    pairs = [(a, b) for i, a in enumerate(printed + dummies) for b in (printed + dummies)[i + 1:]
             if frozenset((a.Name, b.Name)) not in meant]
    for a, b in pairs:
        v = a.Shape.common(b.Shape).Volume
        if v > 0.01:
            ok = False
            out.append(f"FAIL {a.Name} overlaps {b.Name} by {v:.2f} mm3")
    for o in printed:
        v = o.Shape.common(scan_plane(sheet)).Volume
        if v > 0.0001:
            ok = False
            out.append(f"FAIL scan plane crosses {o.Name}")
    imu = doc.getObject("IMU").Shape.BoundBox.Center
    motor = Vector(sheet.frontX + sheet.plateT + sheet.lidarBaseH / 2, 0, sheet.axisZ)
    out.append(f"info IMU to lidar motor {(motor - imu).Length:.0f} mm")
    for name in ("Cell0", "Boost", "UsbSocket", "Switch", "Gland"):
        d = doc.getObject(name).Shape.distToShape(doc.getObject("IMU").Shape)[0]
        out.append(f"info IMU to {name} {d:.0f} mm (nearest)")
    length = doc.getObject("Lidar").Shape.BoundBox.XMax
    out.append(f"info overall length {length:.1f} mm")
    b = doc.getObject("MainPart").Shape.BoundBox
    out.append(f"info main part width {b.YLength:.1f} mm (y {b.YMin:.1f} to {b.YMax:.1f})")
    return ok, out


def colour(objs, rgb, transparency=0):
    if not App.GuiUp:
        return
    for o in objs:
        o.ViewObject.ShapeColor = rgb
        o.ViewObject.Transparency = transparency


def build():
    global doc
    doc = App.newDocument("scanner_case")
    sheet = make_params()
    doc.recompute()

    main, _ = make_main()
    lid = make_lid()
    plate = make_plate()
    frame = make_frame()
    straps = [make_strap(1, P("clampX1")), make_strap(2, P("clampX2"))]
    dummies = make_dummies()
    doc.recompute()

    printed = [main, lid, plate, frame, *straps]
    tops = printed + dummies + [sheet]
    construction = [o for o in doc.Objects if o not in tops]
    for o in construction:
        o.Visibility = False
    group("Printed", printed)
    group("NotPrinted", dummies)
    group("Construction", construction)

    # outer parts see-through so the inside shows; set Transparency to 0 for solid
    colour([main], (0.80, 0.82, 0.80), 60)
    colour([lid], (0.45, 0.50, 0.48), 60)
    colour([plate], (0.90, 0.55, 0.20))
    colour([frame, *straps], (0.95, 0.80, 0.30))
    colour([doc.Lidar], (0.20, 0.20, 0.22))
    colour([doc.Board], (0.20, 0.30, 0.75))
    colour([doc.Cell0, doc.Cell1], (0.20, 0.60, 0.30))
    colour([doc.IMU], (0.10, 0.45, 0.85))
    colour([doc.Boost, doc.UsbSocket, doc.Switch], (0.55, 0.55, 0.55))
    colour([doc.Gland], (0.80, 0.65, 0.25))
    colour([doc.Cable], (0.10, 0.10, 0.10))

    ok, report = checks(printed, dummies, sheet)
    print("\n".join(report))

    os.makedirs(STL_DIR, exist_ok=True)
    for o in printed:
        mesh = MeshPart.meshFromShape(Shape=print_shape(o), LinearDeflection=0.05, AngularDeflection=math.radians(10))
        mesh.write(os.path.join(STL_DIR, f"{o.Name}.stl"))
    doc.saveAs(FCSTD)
    print(f"saved {FCSTD}, STLs in {STL_DIR}")
    print("all checks passed" if ok else "SOME CHECKS FAILED")
    return ok


ok = build()
if App.GuiUp:
    import FreeCADGui as Gui
    if os.environ.get("CASE_BUILD_QUIT"):  # build with colours, then close
        os._exit(0 if ok else 1)
    Gui.ActiveDocument.ActiveView.viewIsometric()
    Gui.SendMsgToActiveView("ViewFit")
elif __name__ == "__main__":
    sys.exit(0 if ok else 1)
