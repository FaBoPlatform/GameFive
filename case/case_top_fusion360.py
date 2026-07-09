# GameFive Case Top (cover) — Fusion 360 script
# - FLAT solid deck (no mound, no window — print in clear material for the LCD)
# - through-holes over all 8 keys
# Mates with GameFive_Case_Bottom (126x67, walls 2.5, 10mm cavity version)
# MIRRORED left-right (x -> OX-x); interior headroom 7mm above the PCB
import adsk.core, adsk.fusion, math

def run(_context):
    app = adsk.core.Application.get()
    doc = app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
    des = adsk.fusion.Design.cast(app.activeProduct)
    root = des.rootComponent
    tbm = adsk.fusion.TemporaryBRepManager.get()

    def mm(v): return v / 10.0

    OX, OY = 126.0, 67.0
    WALL = 2.5
    DECK_IN, DECK_OUT = 5.0, 7.0     # deck inner ceiling / outer surface (seam z=0); 2+5 = 7mm above PCB top
    # tongue (alignment lip into the bottom case pocket)
    TZ0, TZ1 = -1.5, 5.2   # top must overlap the deck underside (z=DECK_IN) to stay one lump
    # buttons (case coords mm) and hole diameters
    HOLES = [   # mirrored: x -> 126 - x
        (105.96, 22.84, 8.0), # UP
        (105.96, 43.16, 8.0), # DOWN
        (116.12, 33.00, 8.0), # LEFT
        (95.80, 33.00, 8.0),  # RIGHT
        (11.29, 39.83, 8.0),  # A
        (28.82, 39.83, 8.0),  # B
        (55.39, 60.28, 8.0),  # START
        (70.63, 60.28, 8.0),  # SELECT
    ]

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
    # 4. alignment tongue (ring into bottom pocket), open at SELECT/START span
    ring = box(2.6, OX-2.6, 2.6, OY-2.6, TZ0, TZ1)
    tbm.booleanOperation(ring, box(4.6, OX-4.6, 4.6, OY-4.6, TZ0-1, TZ1+1), DIFF)
    tbm.booleanOperation(ring, box(44, 82, OY-6, OY+1, TZ0-1, TZ1+1), DIFF)   # gap for SELECT/START switches
    tbm.booleanOperation(body, ring, UNI)
    # 5. button holes
    for (hx, hy, hd) in HOLES:
        tbm.booleanOperation(body, cyl(hx, hy, -4, DECK_OUT+2, hd/2), DIFF)

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
        ("deck solid",          probe(110, 8, 6.0), 0),
        ("cavity empty",        probe(63, 33.5, 2.0), 2),
        ("deck center solid",   probe(63, 30, 6.0), 0),
        ("deck top area solid", probe(63, 8, 6.0), 0),
        ("deck low area solid", probe(63, 54.5, 6.0), 0),
        ("A hole empty",        probe(11.29, 39.83, 6.0), 2),
        ("SELECT hole empty",   probe(70.63, 60.28, 6.0), 2),
        ("D-pad UP hole empty", probe(105.96, 22.84, 6.0), 2),
        ("web deck-RIGHTbtn",   probe(91.1, 33.0, 6.0), 0),
        ("tongue solid",        probe(3.6, 33, -0.75), 0),
        ("tongue gap empty",    probe(63, OY-3.6, -0.75), 2),
        ("skirt wall solid",    probe(1.2, 33, 3.0), 0),
        ("deck left of mound",  probe(31, 12, 6.0), 0),
        ("deck right of mound", probe(100, 10, 6.0), 0),
        ("deck below mound",    probe(30, 60, 6.0), 0),
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
