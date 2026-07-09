# GameFive Case Top (cover) — Fusion 360 script
# - gently raised mound over the display (8mm slope bands, +3mm plateau)
# - through-holes over all 8 keys
# Mates with GameFive_Case_Bottom (126x67, walls 2.5, 10mm cavity version)
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
    DECK_IN, DECK_OUT = 4.0, 6.0     # deck inner ceiling / outer surface (seam z=0)
    M_TOP = 9.0                      # mound plateau top
    # mound base rect (slope bands inside this footprint)
    MX0, MX1 = 35.5, 92.0
    MY0, MY1 = 0.0, 55.3
    SLOPE_W = 8.0                    # slope band width (7.0 on the bottom edge)
    SLOPE_WB = 7.0
    RISE = M_TOP - DECK_OUT          # 3.0
    # tongue (alignment lip into the bottom case pocket)
    TZ0, TZ1 = -1.5, 4.2
    # buttons (case coords mm) and hole diameters
    HOLES = [
        (20.04, 22.84, 8.0),  # UP
        (20.04, 43.16, 8.0),  # DOWN
        (9.88, 33.00, 8.0),   # LEFT
        (30.20, 33.00, 8.0),  # RIGHT
        (114.71, 39.83, 8.0), # A
        (97.18, 39.83, 8.0),  # B
        (70.61, 60.28, 7.0),  # START
        (55.37, 60.28, 7.0),  # SELECT
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

    def tilted_cut(mid, along, second, lenA, lenB, thk):
        # cutting box resting on the slope plane, extending outward-above (normal +z)
        ax = [along[i] for i in range(3)]; bx = [second[i] for i in range(3)]
        n = [ax[1]*bx[2]-ax[2]*bx[1], ax[2]*bx[0]-ax[0]*bx[2], ax[0]*bx[1]-ax[1]*bx[0]]
        if n[2] < 0:
            n = [-v for v in n]   # box is symmetric; only the CENTER offset needs +z normal
        ln = math.sqrt(sum(v*v for v in n)); n = [v/ln for v in n]
        la = math.sqrt(sum(v*v for v in ax)); a = [v/la for v in ax]
        lb = math.sqrt(sum(v*v for v in bx)); b = [v/lb for v in bx]
        c = [mid[i] + n[i]*thk/2 for i in range(3)]
        ob = adsk.core.OrientedBoundingBox3D.create(
            adsk.core.Point3D.create(mm(c[0]), mm(c[1]), mm(c[2])),
            adsk.core.Vector3D.create(a[0], a[1], a[2]),
            adsk.core.Vector3D.create(b[0], b[1], b[2]),
            mm(lenA), mm(lenB), mm(thk))
        return tbm.createBox(ob)

    DIFF = adsk.fusion.BooleanTypes.DifferenceBooleanType
    UNI = adsk.fusion.BooleanTypes.UnionBooleanType

    # 1. skirt + deck
    body = box(0, OX, 0, OY, 0, DECK_OUT)
    tbm.booleanOperation(body, box(WALL, OX-WALL, WALL, OY-WALL, -1, DECK_IN), DIFF)
    # 2. display mound (rect block; slopes carved next)
    tbm.booleanOperation(body, box(MX0, MX1, MY0, MY1, DECK_OUT-0.5, M_TOP), UNI)
    # 3. slope cuts (left, right, top(y0), bottom(y1))
    # each cutting box STARTS at the mound base edge (plane z=DECK_OUT) and
    # extends up-inward only, so the deck outside the mound is never touched
    yc = (MY0+MY1)/2; xc = (MX0+MX1)/2
    LEN_A = 25.0
    cuts = [
        ((MX0, yc, DECK_OUT), ( SLOPE_W, 0, RISE), (0, 1, 0), OY+20),
        ((MX1, yc, DECK_OUT), (-SLOPE_W, 0, RISE), (0, 1, 0), OY+20),
        ((xc, MY0, DECK_OUT), (0,  SLOPE_W, RISE), (1, 0, 0), OX+20),
        ((xc, MY1, DECK_OUT), (0, -SLOPE_WB, RISE), (1, 0, 0), OX+20),
    ]
    for p0, along, second, lenB in cuts:
        la = math.sqrt(sum(v*v for v in along)); d = [v/la for v in along]
        mid = [p0[i] + d[i]*LEN_A/2 for i in range(3)]
        tbm.booleanOperation(body, tilted_cut(mid, along, second, LEN_A, lenB, 12), DIFF)
    # 4. alignment tongue (ring into bottom pocket), open at SELECT/START span
    ring = box(2.6, OX-2.6, 2.6, OY-2.6, TZ0, TZ1)
    tbm.booleanOperation(ring, box(4.6, OX-4.6, 4.6, OY-4.6, TZ0-1, TZ1+1), DIFF)
    tbm.booleanOperation(ring, box(44, 82, OY-6, OY+1, TZ0-1, TZ1+1), DIFF)   # gap for SELECT/START switches
    tbm.booleanOperation(body, ring, UNI)
    # 5. button holes
    for (hx, hy, hd) in HOLES:
        tbm.booleanOperation(body, cyl(hx, hy, -4, M_TOP+2, hd/2), DIFF)

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
        ("deck solid",          probe(110, 8, 5.0), 0),
        ("cavity empty",        probe(63, 33.5, 2.0), 2),
        ("plateau solid",       probe(63, 27, 8.5), 0),
        ("above plateau",       probe(63, 27, 9.4), 2),
        ("slope low solid",     probe(37, 27, 6.3), 0),
        ("slope high empty",    probe(37, 27, 7.6), 2),
        ("slope btm solid",     probe(63, 53.5, 6.3), 0),
        ("slope btm empty",     probe(63, 53.5, 7.9), 2),
        ("A hole empty",        probe(114.71, 39.83, 5.0), 2),
        ("SELECT hole empty",   probe(55.37, 60.28, 5.0), 2),
        ("D-pad UP hole empty", probe(20.04, 22.84, 5.0), 2),
        ("web RIGHT-mound",     probe(34.9, 33.0, 5.0), 0),
        ("tongue solid",        probe(3.6, 33, -0.75), 0),
        ("tongue gap empty",    probe(63, OY-3.6, -0.75), 2),
        ("skirt wall solid",    probe(1.2, 33, 3.0), 0),
        ("deck left of mound",  probe(31, 12, 5.0), 0),
        ("deck right of mound", probe(105, 20, 5.0), 0),
        ("deck below mound",    probe(30, 60, 5.0), 0),
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
