# HandyGame2 case rebuild (latest: notch-matched island position, closed frame)
# Run via Fusion MCP fusion_mcp_execute (script) — creates a NEW doc, exports 3 files.
import adsk.core, adsk.fusion

def run(_context):
    app = adsk.core.Application.get()
    doc = app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
    design = adsk.fusion.Design.cast(app.activeProduct)
    root = design.rootComponent
    tbm = adsk.fusion.TemporaryBRepManager.get()

    def box(x1, x2, y1, y2, z1, z2):
        cx, cy, cz = (x1+x2)/2.0, (y1+y2)/2.0, (z1+z2)/2.0
        obb = adsk.core.OrientedBoundingBox3D.create(
            adsk.core.Point3D.create(cx, cy, cz),
            adsk.core.Vector3D.create(1,0,0), adsk.core.Vector3D.create(0,1,0),
            abs(x2-x1), abs(y2-y1), abs(z2-z1))
        return tbm.createBox(obb)

    def cyl(x, y, z1, z2, r):
        return tbm.createCylinderOrCone(
            adsk.core.Point3D.create(x,y,z1), r,
            adsk.core.Point3D.create(x,y,z2), r)

    DIFF = adsk.fusion.BooleanTypes.DifferenceBooleanType
    UNION = adsk.fusion.BooleanTypes.UnionBooleanType
    MH = [(0.635,0.635),(11.364,0.635),(0.635,7.864),(11.364,7.864)]

    bot = box(-0.25,12.25, -0.25,8.75, 0.0,1.56)
    tbm.booleanOperation(bot, box(-0.03,12.03, -0.03,8.53, 1.20,1.60), DIFF)
    tbm.booleanOperation(bot, box(0.12,11.88, 0.12,8.38, 0.20,1.20), DIFF)
    for (mx,my) in MH:
        tbm.booleanOperation(bot, cyl(mx,my,0.0,1.20,0.25), UNION)
    for (mx,my) in MH:
        tbm.booleanOperation(bot, cyl(mx,my,0.35,1.21,0.105), DIFF)
    tbm.booleanOperation(bot, box(11.50,12.35, 1.686,2.886, 0.80,1.30), DIFF)  # USB right wall
    bf = root.features.baseFeatures.add()
    bf.startEdit()
    b1 = root.bRepBodies.add(bot, bf)
    b1.name = 'CaseBottom'
    bf.finishEdit()

    top = box(-0.25,12.25, -0.25,8.75, 1.56,1.86)
    tbm.booleanOperation(top, box(0.0,12.0, 0.0,8.5, 1.56,1.66), DIFF)
    tbm.booleanOperation(top, box(3.76,8.24, 0.2,6.05, 1.56,2.71), UNION)   # island (panel y0.483..5.662)
    tbm.booleanOperation(top, box(3.96,8.04, 0.4,5.85, 1.56,2.46), DIFF)    # window cavity (closed frame)
    tbm.booleanOperation(top, box(1.079,2.329, 2.677,5.959, 1.50,1.95), DIFF)  # cross vert
    tbm.booleanOperation(top, box(0.15,3.345, 3.693,4.943, 1.50,1.95), DIFF)   # cross horiz
    tbm.booleanOperation(top, box(8.793,11.796, 3.693,4.943, 1.50,1.95), DIFF) # A/B
    tbm.booleanOperation(top, box(4.782,7.206, 7.297,8.197, 1.50,1.95), DIFF)  # SELECT/START
    for (mx,my) in MH:
        tbm.booleanOperation(top, cyl(mx,my,1.50,1.95,0.15), DIFF)
    bf2 = root.features.baseFeatures.add()
    bf2.startEdit()
    b2 = root.bRepBodies.add(top, bf2)
    b2.name = 'CaseTop'
    bf2.finishEdit()

    corners = [(-0.25,-0.25),(12.25,-0.25),(-0.25,8.75),(12.25,8.75)]
    for body in [root.bRepBodies.itemByName('CaseBottom'), root.bRepBodies.itemByName('CaseTop')]:
        coll = adsk.core.ObjectCollection.create()
        for e in body.edges:
            v0 = e.startVertex.geometry
            v1 = e.endVertex.geometry
            if abs(v0.x-v1.x) < 1e-6 and abs(v0.y-v1.y) < 1e-6:
                for (cx,cy) in corners:
                    if abs(v0.x-cx) < 1e-4 and abs(v0.y-cy) < 1e-4 and abs(v0.z-v1.z) > 0.25:
                        coll.add(e)
        if coll.count > 0:
            fin = root.features.filletFeatures.createInput()
            fin.addConstantRadiusEdgeSet(coll, adsk.core.ValueInput.createByReal(0.3), True)
            root.features.filletFeatures.add(fin)

    em = design.exportManager
    base = '/Users/akira/Documents/workspace_pcb/claude/'
    bot_b = root.bRepBodies.itemByName('CaseBottom')
    top_b = root.bRepBodies.itemByName('CaseTop')
    assert bot_b.lumps.count == 1 and top_b.lumps.count == 1
    em.execute(em.createSTLExportOptions(bot_b, base + 'HandyGame2_Case_Bottom.stl'))
    em.execute(em.createSTLExportOptions(top_b, base + 'HandyGame2_Case_Top.stl'))
    em.execute(em.createSTEPExportOptions(base + 'HandyGame2_Case_Assembly.step', root))
    print('lumps', bot_b.lumps.count, top_b.lumps.count, '- exported')
