# GameFive Case Bottom — Fusion 360 script (run via MCP bridge)
# Board: GameFive rev.2k, 121.92 x 52.83 x 1.6mm, R3.81 corners
# MIRRORED left-right (x -> OX-x) so the printed case matches the physical board
# Top wall carries all I/O: USB-C (XIAO, top edge), microSD slot (back side),
# power-switch knob slot (front side). 10mm cavity under the PCB for speaker + LiPo.
import adsk.core, adsk.fusion

def run(_context):
    app = adsk.core.Application.get()
    doc = app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
    des = adsk.fusion.Design.cast(app.activeProduct)
    root = des.rootComponent
    tbm = adsk.fusion.TemporaryBRepManager.get()

    def mm(v): return v / 10.0  # mm -> cm (Fusion internal unit)

    # ---- parameters (mm) ----
    BW, BH = 121.92, 52.83        # board size (4800 x 2080 mil)
    CLR = 0.5                     # board clearance per side
    WALL = 2.5
    FLOOR = 2.0
    CAV = 10.0                    # under-PCB cavity (speaker + LiPo)
    PCB_T = 1.6
    RIM = 2.0                     # wall above PCB top
    LEDGE = 1.5

    OX = BW + 2*(CLR + WALL)      # 127.92
    OY = BH + 2*(CLR + WALL)      # 58.83
    OZ = FLOOR + CAV + PCB_T + RIM  # 15.6
    Z_PCB = FLOOR + CAV           # 12.0 PCB underside

    OFF = WALL + CLR              # 3.0 board-local -> case offset
    # mounting holes (board-local mm, y from top edge); symmetric in x -> mirror-safe
    MH = [(6.35, 6.35), (115.57, 6.35), (6.35, 46.48), (115.57, 46.48)]
    BOSS_D, PILOT_D, PILOT_DEPTH = 7.0, 2.1, 8.0
    # XIAO USB-C at the TOP board edge (module on the back, USB below the PCB),
    # board-local x center 98.17mm -> mirrored case x
    USB_XC = OX - (98.17 + OFF)   # 26.75
    USB_W = 14.0
    USB_Z0 = Z_PCB - 6.0          # slot bottom 6mm below PCB underside (drop-in, open top)
    # microSD (back side, card ejects through the top wall below the PCB)
    SD_XC = OX - (40.01 + OFF)    # 84.91
    SD_W = 13.0                   # card 11mm + clearance
    SD_Z0, SD_Z1 = 9.6, 12.2      # card plane (connector 1.85mm under the board)
    NOTCH_W, NOTCH_Z0 = 6.0, 8.2  # fingertip notch to reach the card end
    # power slide-switch knob (front side, crosses the seam; slot continues in the lid)
    SW_XC = OX - (22.86 + OFF)    # 102.06
    SW_W = 6.0                    # knob 2mm + ~2mm travel + clearance
    SW_Z0 = 13.8                  # just above PCB top (13.6), open to the seam

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
    # lower cavity, inset LEDGE from pocket walls
    tbm.booleanOperation(body, box(WALL+LEDGE, OX-WALL-LEDGE, WALL+LEDGE, OY-WALL-LEDGE, FLOOR, Z_PCB), DIFF)
    # corner bosses up to PCB level (overlap floor: z from 0)
    for (bx, by) in MH:
        tbm.booleanOperation(body, cyl(bx+OFF, by+OFF, 0, Z_PCB, BOSS_D/2), UNI)
    # pilot holes (M2.5 self-tap) from boss top down
    for (bx, by) in MH:
        tbm.booleanOperation(body, cyl(bx+OFF, by+OFF, Z_PCB-PILOT_DEPTH, Z_PCB+0.2, PILOT_D/2), DIFF)
    # USB-C drop-in slot in the TOP wall (through wall + ledge, open to top)
    tbm.booleanOperation(body, box(USB_XC-USB_W/2, USB_XC+USB_W/2, -1.0, WALL+LEDGE+0.7, USB_Z0, OZ+1), DIFF)
    # microSD slot in the TOP wall at card level; channel continues through the ledge
    tbm.booleanOperation(body, box(SD_XC-SD_W/2, SD_XC+SD_W/2, -1.0, WALL+LEDGE+2.3, SD_Z0, SD_Z1), DIFF)
    # fingertip notch (deeper, centered) so the ejected card can be pinched
    tbm.booleanOperation(body, box(SD_XC-NOTCH_W/2, SD_XC+NOTCH_W/2, -1.0, WALL+0.7, NOTCH_Z0, SD_Z1), DIFF)
    # power-switch knob slot in the TOP wall rim (open to the seam; lid continues it)
    tbm.booleanOperation(body, box(SW_XC-SW_W/2, SW_XC+SW_W/2, -1.0, WALL+0.7, SW_Z0, OZ+1), DIFF)
    # speaker grille: concentric rings of D2 holes in the floor (speaker bay near J2)
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
        ("floor solid",           probe(63, 30, 1.0), 0),
        ("cavity empty",          probe(63, 30, 9.0), 2),
        ("pocket empty",          probe(63, 30, 14.0), 2),
        ("ledge solid",           probe(63, WALL+LEDGE/2, 10.0), 0),
        ("top wall solid",        probe(63, 1.2, 12.0), 0),
        ("boss solid",            probe(MH[0][0]+OFF+2.4, MH[0][1]+OFF, 10.0), 0),
        ("pilot empty",           probe(MH[0][0]+OFF, MH[0][1]+OFF, 10.0), 2),
        ("USB slot empty",        probe(USB_XC, 1.5, 9.0), 2),
        ("USB slot open at top",  probe(USB_XC, 1.5, 15.0), 2),
        ("USB ledge channel",     probe(USB_XC, WALL+LEDGE/2, 9.0), 2),
        ("SD slot empty",         probe(SD_XC, 1.5, 11.0), 2),
        ("SD ledge channel",      probe(SD_XC, WALL+LEDGE/2, 11.0), 2),
        ("SD notch empty",        probe(SD_XC, 1.5, 8.8), 2),
        ("wall above SD solid",   probe(SD_XC, 1.2, 13.5), 0),
        ("SW9 slot empty",        probe(SW_XC, 1.5, 14.5), 2),
        ("wall below SW9 solid",  probe(SW_XC, 1.2, 12.5), 0),
        ("grille hole empty",     probe(GX, GY, 1.0), 2),
        ("grille web solid",      probe(GX+2.0, GY, 1.0), 0),
        ("bottom wall solid",     probe(63, OY-1.2, 12.0), 0),
        ("left wall solid",       probe(1.2, 30, 12.0), 0),
        ("right wall solid",      probe(OX-1.2, 30, 12.0), 0),
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
