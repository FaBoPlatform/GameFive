# GameFive Case Bottom — Fusion 360 script (run via MCP bridge)
# Board: GameFive rev.2i, 120x61x1.6mm, R3.81 corners
# MIRRORED left-right (x -> OX-x) so the printed case matches the physical board
# Requirement: 15mm cavity UNDER the PCB for speaker + LiPo battery
import adsk.core, adsk.fusion

def run(_context):
    app = adsk.core.Application.get()
    doc = app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
    des = adsk.fusion.Design.cast(app.activeProduct)
    root = des.rootComponent
    tbm = adsk.fusion.TemporaryBRepManager.get()

    def mm(v): return v / 10.0  # mm -> cm (Fusion internal unit)

    # ---- parameters (mm) ----
    BW, BH = 120.0, 61.0          # board size
    CLR = 0.5                     # board clearance per side
    WALL = 2.5
    FLOOR = 2.0
    CAV = 10.0                    # under-PCB cavity (speaker + LiPo)
    PCB_T = 1.6
    RIM = 2.0                     # wall above PCB top
    LEDGE = 1.5

    OX = BW + 2*(CLR + WALL)      # 126
    OY = BH + 2*(CLR + WALL)      # 67
    OZ = FLOOR + CAV + PCB_T + RIM  # 20.6
    Z_PCB = FLOOR + CAV           # 17.0 PCB underside

    # board-local -> case offset
    OFF = WALL + CLR              # 3.0
    # mounting holes (board-local mm, y from top edge)
    MH = [(6.35, 6.35), (113.64, 6.35), (6.35, 54.66), (113.64, 54.66)]
    BOSS_D, PILOT_D, PILOT_DEPTH = 7.0, 2.1, 8.0
    # XIAO USB on RIGHT edge, board-local y center 17.78mm
    USB_YC = 17.78 + OFF
    USB_W = 14.0
    USB_Z0 = FLOOR + CAV - 6.0    # slot bottom 6mm below PCB underside

    def box(x0, x1, y0, y1, z0, z1):
        cx, cy, cz = mm((x0+x1)/2), mm((y0+y1)/2), mm((z0+z1)/2)
        ob = adsk.core.OrientedBoundingBox3D.create(
            adsk.core.Point3D.create(cx, cy, cz),
            adsk.core.Vector3D.create(1, 0, 0), adsk.core.Vector3D.create(0, 1, 0),
            mm(x1-x0), mm(y1-y0), mm(z1-z0))
        return tbm.createBox(ob)

    def cyl(x, y, z0, z1, r):
        return tbm.createCylinderOrCone(
            adsk.core.Point3D.create(mm(x), mm(y), mm(z0)), mm(r),
            adsk.core.Point3D.create(mm(x), mm(y), mm(z1)), mm(r))

    DIFF = adsk.fusion.BooleanTypes.DifferenceBooleanType
    UNI = adsk.fusion.BooleanTypes.UnionBooleanType

    body = box(0, OX, 0, OY, 0, OZ)
    # PCB pocket (top open): full clearance area from Z_PCB up
    tbm.booleanOperation(body, box(WALL, OX-WALL, WALL, OY-WALL, Z_PCB, OZ+1), DIFF)
    # lower cavity (15mm), inset LEDGE from pocket walls
    tbm.booleanOperation(body, box(WALL+LEDGE, OX-WALL-LEDGE, WALL+LEDGE, OY-WALL-LEDGE, FLOOR, Z_PCB), DIFF)
    # corner bosses up to PCB level (overlap floor: z from 0)
    for (bx, by) in MH:
        tbm.booleanOperation(body, cyl(bx+OFF, by+OFF, 0, Z_PCB, BOSS_D/2), UNI)
    # pilot holes (M2.5 self-tap) from boss top down
    for (bx, by) in MH:
        tbm.booleanOperation(body, cyl(bx+OFF, by+OFF, Z_PCB-PILOT_DEPTH, Z_PCB+0.2, PILOT_D/2), DIFF)
    # USB-C slot in LEFT wall after mirroring (open to top so board+XIAO drops in)
    tbm.booleanOperation(body, box(-1.0, 7.0, USB_YC-USB_W/2, USB_YC+USB_W/2, USB_Z0, OZ+1), DIFF)
    # speaker grille: concentric rings of D2 holes in the floor, near J2 (board bottom-right)
    import math
    GX, GY = 26.0, 40.0             # grille center (case coords, mm, mirrored)
    holes = [(GX, GY)]
    for ring, n in ((4.0, 6), (8.0, 12), (12.0, 18)):
        for k in range(n):
            a = 2*math.pi*k/n
            holes.append((GX + ring*math.cos(a), GY + ring*math.sin(a)))
    for (hx, hy) in holes:
        tbm.booleanOperation(body, cyl(hx, hy, -1, FLOOR+0.5, 1.0), DIFF)

    bf = root.features.baseFeatures.add()
    bf.startEdit()
    b = root.bRepBodies.add(body, bf)
    b.name = "GameFive_Case_Bottom"
    bf.finishEdit()

    # fillet 4 outer vertical corners R3.81
    b = root.bRepBodies.itemByName("GameFive_Case_Bottom")
    corners = [(0, 0), (OX, 0), (0, OY), (OX, OY)]
    coll = adsk.core.ObjectCollection.create()
    for e in b.edges:
        v0, v1 = e.startVertex.geometry, e.endVertex.geometry
        if abs(v0.x - v1.x) < 1e-6 and abs(v0.y - v1.y) < 1e-6:
            for (cx, cy) in corners:
                if abs(v0.x - mm(cx)) < 1e-4 and abs(v0.y - mm(cy)) < 1e-4:
                    coll.add(e)
    if coll.count:
        fi = root.features.filletFeatures.createInput()
        fi.edgeSetInputs.addConstantRadiusEdgeSet(coll, adsk.core.ValueInput.createByReal(mm(3.81)), True)
        root.features.filletFeatures.add(fi)

    # R1 rounding on the bottom-face outer perimeter
    b = root.bRepBodies.itemByName("GameFive_Case_Bottom")
    coll2 = adsk.core.ObjectCollection.create()
    for e in b.edges:
        v0, v1 = e.startVertex.geometry, e.endVertex.geometry
        if abs(v0.z) < 1e-5 and abs(v1.z) < 1e-5:
            bb = e.boundingBox
            on_edge = (bb.minPoint.x < mm(1.0) or bb.maxPoint.x > mm(OX-1.0) or
                       bb.minPoint.y < mm(1.0) or bb.maxPoint.y > mm(OY-1.0))
            if on_edge:
                coll2.add(e)
    if coll2.count:
        fi2 = root.features.filletFeatures.createInput()
        fi2.edgeSetInputs.addConstantRadiusEdgeSet(coll2, adsk.core.ValueInput.createByReal(mm(1.0)), True)
        root.features.filletFeatures.add(fi2)

    # ---- verification ----
    b = root.bRepBodies.itemByName("GameFive_Case_Bottom")
    print("lumps:", b.lumps.count)
    um = des.unitsManager
    bb = b.boundingBox
    print("bbox mm:", round(um.convert(bb.maxPoint.x-bb.minPoint.x, um.internalUnits, 'mm'), 2),
          round(um.convert(bb.maxPoint.y-bb.minPoint.y, um.internalUnits, 'mm'), 2),
          round(um.convert(bb.maxPoint.z-bb.minPoint.z, um.internalUnits, 'mm'), 2))
    def probe(x, y, z):
        return int(b.pointContainment(adsk.core.Point3D.create(mm(x), mm(y), mm(z))))
    # 0=inside solid, 2=outside
    checks = [
        ("USB slot open at top",  probe(1.5, USB_YC, 15.0), 2),
        ("floor solid",        probe(63, 33.5, 1.0), 0),
        ("cavity empty",       probe(63, 33.5, 9.0), 2),
        ("ledge solid",        probe(63, WALL+LEDGE/2, 10.0), 0),
        ("boss solid",         probe(MH[0][0]+OFF+2.4, MH[0][1]+OFF, 10.0), 0),
        ("pilot empty",        probe(MH[0][0]+OFF, MH[0][1]+OFF, 10.0), 2),
        ("USB slot empty",     probe(1.5, USB_YC, 9.0), 2),
        ("wall solid",         probe(1.2, 50.0, 12.0), 0),
        ("pocket empty",       probe(63, 33.5, 18.0), 2),
        ("grille hole empty",  probe(26.0, 40.0, 1.0), 2),
        ("grille web solid",   probe(28.0, 40.0, 1.0), 0),
        ("USB thru (ledge zone)", probe(3.75, USB_YC, 9.0), 2),
        ("USB thru (wall zone)",  probe(1.0, USB_YC, 9.0), 2),
    ]
    ok = True
    for name, got, want in checks:
        good = (got == want)
        ok = ok and good
        print(("OK " if good else "NG ") + name, got, "want", want)

    if ok and b.lumps.count == 1:
        em = des.exportManager
        stl = em.createSTLExportOptions(b, '/Users/akira/Documents/workspace_pcb/claude/GameFive/case/GameFive_Case_Bottom.stl')
        em.execute(stl)
        step = em.createSTEPExportOptions('/Users/akira/Documents/workspace_pcb/claude/GameFive/case/GameFive_Case_Bottom.step', root)
        em.execute(step)
        print("EXPORTED")
    else:
        print("EXPORT SKIPPED - verification failed")
