# GameFive Case Top (cover) — Fusion 360 script, board rev.2k (121.92 x 52.83)
# - FLAT solid deck (no mound, no window — print in clear material for the LCD)
# - through-holes over all 8 keys, M2.5 through-screw clearance at the 4 corners
# - power-switch knob slot continues through the skirt/tongue on the top wall
# Mates with GameFive_Case_Bottom (127.92 x 58.83, walls 2.5, 10mm cavity, seam z=15.6)
# MIRRORED left-right (x -> OX-x); interior headroom 7mm above the PCB
import adsk.core, adsk.fusion, math

def run(_context):
    app = adsk.core.Application.get()
    doc = app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
    des = adsk.fusion.Design.cast(app.activeProduct)
    root = des.rootComponent
    tbm = adsk.fusion.TemporaryBRepManager.get()

    def mm(v): return v / 10.0

    OX, OY = 127.92, 58.83
    WALL = 2.5
    DECK_IN, DECK_OUT = 5.0, 7.0     # deck inner ceiling / outer surface (seam z=0)
    TZ0, TZ1 = -1.5, 5.2             # alignment tongue (overlaps deck underside -> one lump)
    # buttons (case coords mm, x mirrored = OX - (board_mil*0.0254 + 3.0)) and hole diameters
    HOLES = [
        (108.71, 17.55, 10.0),  # UP      SW1 (638,-573)   EVQQ1 plunger 6.2
        (108.71, 37.87, 10.0),  # DOWN    SW2 (638,-1373)
        (118.87, 27.71, 10.0),  # LEFT    SW3 (238,-973)
        (98.55, 27.71, 10.0),   # RIGHT   SW4 (1038,-973)
        (11.86, 34.04, 10.0),   # A       SW5 (4451,-1222)
        (29.39, 34.04, 10.0),   # B       SW6 (3761,-1222)
        (56.85, 51.64, 5.0),    # START   SW7 (2680,-1915) B3F plunger 3.5
        (71.07, 51.64, 5.0),    # SELECT  SW8 (2120,-1915)
    ]
    # M2.5 through-screws: TOP deck -> PCB MH (2.8) -> bottom boss pilot (2.1)
    MH = [(9.35, 9.35), (118.57, 9.35), (9.35, 49.48), (118.57, 49.48)]
    # power-switch knob slot (top wall, knob spans the seam: bottom-coords z13.8..17.4)
    SW_XC, SW_W = 102.06, 6.0

    def box(x0, x1, y0, y1, z0, z1):
        ob = adsk.core.OrientedBoundingBox3D.create(
            adsk.core.Point3D.create(mm((x0+x1)/2), mm((y0+y1)/2), mm((z0+z1)/2)),
            adsk.core.Vector3D.create(1, 0, 0), adsk.core.Vector3D.create(0, 1, 0),
            mm(x1-x0), mm(y1-y0), mm(z1-z0))
        return tbm.createBox(ob)

    def cyl(x, y, z0, z1, r):
        return tbm.createCylinderOrCone(
            adsk.core.Point3D.create(mm(x), mm(y), mm(z0)), mm(r),
            adsk.core.Point3D.create(mm(x), mm(y), mm(z1)), mm(r))

    DIFF = adsk.fusion.BooleanTypes.DifferenceBooleanType
    UNI = adsk.fusion.BooleanTypes.UnionBooleanType

    # 1. skirt + flat deck
    body = box(0, OX, 0, OY, 0, DECK_OUT)
    tbm.booleanOperation(body, box(WALL, OX-WALL, WALL, OY-WALL, -1, DECK_IN), DIFF)
    # 2. alignment tongue (ring into bottom pocket), open at SELECT/START span
    ring = box(2.6, OX-2.6, 2.6, OY-2.6, TZ0, TZ1)
    tbm.booleanOperation(ring, box(4.6, OX-4.6, 4.6, OY-4.6, TZ0-1, TZ1+1), DIFF)
    tbm.booleanOperation(ring, box(52.85, 75.07, OY-6, OY+1, TZ0-1, TZ1+1), DIFF)  # SELECT/START gap
    tbm.booleanOperation(body, ring, UNI)
    # 3. power-switch knob slot through skirt + tongue (knob top = +1.8 above seam)
    tbm.booleanOperation(body, box(SW_XC-SW_W/2, SW_XC+SW_W/2, -1.0, 4.7, -2.0, 1.8), DIFF)
    # 4. button holes + screw clearance holes
    for (hx, hy, hd) in HOLES:
        tbm.booleanOperation(body, cyl(hx, hy, -4, DECK_OUT+2, hd/2), DIFF)
    for (mx, my) in MH:
        tbm.booleanOperation(body, cyl(mx, my, -4, DECK_OUT+2, 1.4), DIFF)

    bf = root.features.baseFeatures.add()
    bf.startEdit()
    b = root.bRepBodies.add(body, bf)
    b.name = "GameFive_Case_Top"
    bf.finishEdit()

    # fillet outer vertical corners R3.81 (skirt region)
    b = root.bRepBodies.itemByName("GameFive_Case_Top")
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

    # R1 rounding on the top-face outer perimeter
    b = root.bRepBodies.itemByName("GameFive_Case_Top")
    coll2 = adsk.core.ObjectCollection.create()
    zt = mm(DECK_OUT)
    for e in b.edges:
        v0, v1 = e.startVertex.geometry, e.endVertex.geometry
        if abs(v0.z - zt) < 1e-5 and abs(v1.z - zt) < 1e-5:
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
    b = root.bRepBodies.itemByName("GameFive_Case_Top")
    print("lumps:", b.lumps.count)
    um = des.unitsManager
    bb = b.boundingBox
    print("bbox mm:", round(um.convert(bb.maxPoint.x-bb.minPoint.x, um.internalUnits, 'mm'), 2),
          round(um.convert(bb.maxPoint.y-bb.minPoint.y, um.internalUnits, 'mm'), 2),
          round(um.convert(bb.maxPoint.z-bb.minPoint.z, um.internalUnits, 'mm'), 2))
    def probe(x, y, z):
        return int(b.pointContainment(adsk.core.Point3D.create(mm(x), mm(y), mm(z))))
    checks = [
        ("deck center solid",    probe(63, 27, 6.0), 0),
        ("deck top strip solid", probe(63, 8, 6.0), 0),
        ("deck low strip solid", probe(90, 47, 6.0), 0),
        ("cavity empty",         probe(63, 30, 2.0), 2),
        ("UP hole empty",        probe(108.71, 17.55, 6.0), 2),
        ("DOWN hole empty",      probe(108.71, 37.87, 6.0), 2),
        ("LEFT hole empty",      probe(118.87, 27.71, 6.0), 2),
        ("RIGHT hole empty",     probe(98.55, 27.71, 6.0), 2),
        ("A hole empty",         probe(11.86, 34.04, 6.0), 2),
        ("B hole empty",         probe(29.39, 34.04, 6.0), 2),
        ("START hole empty",     probe(56.85, 51.64, 6.0), 2),
        ("SELECT hole empty",    probe(71.07, 51.64, 6.0), 2),
        ("A-B web solid",        probe(20.6, 34.04, 6.0), 0),
        ("dpad center web",      probe(108.71, 27.71, 6.0), 0),
        ("screw hole empty",     probe(9.35, 9.35, 6.0), 2),
        ("screw web solid",      probe(9.35+2.6, 9.35, 6.0), 0),
        ("SW9 slot cut empty",   probe(SW_XC, 1.2, 0.8), 2),
        ("skirt near SW9 solid", probe(92.0, 1.2, 3.0), 0),
        ("tongue solid",         probe(63, 3.6, -0.75), 0),
        ("tongue gap empty",     probe(63, OY-3.6, -0.75), 2),
        ("skirt wall solid",     probe(1.2, 30, 3.0), 0),
    ]
    ok = True
    for name, got, want in checks:
        good = (got == want)
        ok = ok and good
        print(("OK " if good else "NG ") + name, got, "want", want)

    if ok and b.lumps.count == 1:
        em = des.exportManager
        stl = em.createSTLExportOptions(b, '/Users/akira/Documents/workspace_pcb/claude/GameFive/case/GameFive_Case_Top.stl')
        em.execute(stl)
        step = em.createSTEPExportOptions('/Users/akira/Documents/workspace_pcb/claude/GameFive/case/GameFive_Case_Top.step', root)
        em.execute(step)
        print("EXPORTED")
    else:
        print("EXPORT SKIPPED - verification failed")
