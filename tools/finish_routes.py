#!/usr/bin/env python3
"""Verlegt die Verbindungen, die FreeRouting offen laesst, mit einem einfachen
Rastersuch-Router (A*, 0,25-mm-Raster, 2 Lagen, Via-Wechsel erlaubt).

Quelle der offenen Stellen: `kicad-cli pcb drc --format json` (Eintrag
`unconnected_items` mit den beiden Positionen). Damit werden Zonenanbindungen
korrekt beruecksichtigt -- es werden nur echte Luecken geschlossen.

    /usr/bin/python3 tools/finish_routes.py

Wird von tools/route_daughtercard.py nach dem SES-Import und dem Fuellen der
Masseflaechen aufgerufen; laesst sich auch einzeln starten.
"""
from __future__ import annotations

import heapq
import json
import math
import subprocess
import sys
import tempfile
from pathlib import Path
from shutil import which

import pcbnew

PCB = Path(__file__).resolve().parent.parent / "hardware" / "daughtercard" / "daughtercard.kicad_pcb"
GRID = 0.25
CLEAR = 0.25
TRACK_W = 0.5
VIA_D, VIA_DRILL = 0.6, 0.3
VIA_COST = 10


def _seg_pt(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def drc_unconnected(board_path: Path):
    cli = ["kicad-cli"]
    if which("xvfb-run"):
        cli = ["xvfb-run", "-a", *cli]
    with tempfile.TemporaryDirectory() as td:
        out = Path(td) / "drc.json"
        subprocess.run([*cli, "pcb", "drc", "--format", "json", "-o", str(out),
                        str(board_path)], check=True, capture_output=True)
        data = json.loads(out.read_text())
    pairs = []
    for u in data.get("unconnected_items", []):
        its = u.get("items", [])
        if len(its) == 2 and all("pos" in i for i in its):
            out = []
            for i in its:
                d = i.get("description", "")
                if "on F.Cu" in d and "on B.Cu" not in d:
                    lay = [pcbnew.F_Cu]
                elif "on B.Cu" in d and "on F.Cu" not in d:
                    lay = [pcbnew.B_Cu]
                elif "PTH pad" in d or "Via" in d:
                    lay = [pcbnew.F_Cu, pcbnew.B_Cu]
                else:
                    lay = []
                out.append(((i["pos"]["x"], i["pos"]["y"]), lay))
            pairs.append(tuple(out))
    return pairs


def block_grids(board, keep_net, bbox):
    """Boolesche Sperr-Raster je Lage (True = belegt). Einmal rasterisieren,
    danach nur noch O(1)-Lookups im A*."""
    import numpy as np
    x0, y0, x1, y1 = bbox
    W = int((x1 - x0) / GRID) + 1
    H = int((y1 - y0) / GRID) + 1
    need = TRACK_W / 2 + CLEAR
    grids = {pcbnew.F_Cu: np.zeros((W, H), bool), pcbnew.B_Cu: np.zeros((W, H), bool)}

    def stamp(g, x, y, rad):
        cx = (x - x0) / GRID
        cy = (y - y0) / GRID
        rr = rad / GRID
        i0, i1 = max(0, int(cx - rr)), min(W - 1, int(cx + rr) + 1)
        j0, j1 = max(0, int(cy - rr)), min(H - 1, int(cy + rr) + 1)
        for i in range(i0, i1 + 1):
            for j in range(j0, j1 + 1):
                if (i - cx) ** 2 + (j - cy) ** 2 <= rr * rr:
                    g[i, j] = True

    def stamp_seg(g, ax, ay, bx, by, rad):
        n = max(1, int(math.hypot(bx - ax, by - ay) / (GRID / 2)))
        for k in range(n + 1):
            t = k / n
            stamp(g, ax + (bx - ax) * t, ay + (by - ay) * t, rad)

    for t in board.GetTracks():
        if t.GetNetCode() == keep_net:
            continue
        if t.GetClass() == "PCB_VIA":
            p = t.GetPosition()
            for g in grids.values():
                stamp(g, pcbnew.ToMM(p.x), pcbnew.ToMM(p.y), 0.45 + need)
        else:
            s, e = t.GetStart(), t.GetEnd()
            hw = pcbnew.ToMM(t.GetWidth()) / 2
            stamp_seg(grids[t.GetLayer()], pcbnew.ToMM(s.x), pcbnew.ToMM(s.y),
                      pcbnew.ToMM(e.x), pcbnew.ToMM(e.y), hw + need)
    for f in board.GetFootprints():
        for pad in f.Pads():
            if pad.GetNetCode() == keep_net:
                continue
            p = pad.GetPosition()
            rad = pcbnew.ToMM(pad.GetBoundingRadius()) + need
            for lay, g in grids.items():
                if pad.IsOnLayer(lay):
                    stamp(g, pcbnew.ToMM(p.x), pcbnew.ToMM(p.y), rad)
    return grids, W, H


def astar(grids, W, H, start_xy, goal_xy, bbox, start_layers, goal_layers):
    x0, y0 = bbox[0], bbox[1]
    LF, LB = pcbnew.F_Cu, pcbnew.B_Cu

    def cell(xy):
        return (min(W - 1, max(0, round((xy[0] - x0) / GRID))),
                min(H - 1, max(0, round((xy[1] - y0) / GRID))))

    gx, gy = cell(goal_xy)
    sx, sy = cell(start_xy)
    goal_layers = set(goal_layers) or {LF, LB}
    start_layers = list(start_layers) or [LF, LB]

    def free(l, i, j):
        return not grids[l][i, j]

    openq = [(0.0, 0.0, (sx, sy, l)) for l in start_layers]
    came, best = {}, {n: 0.0 for _, _, n in openq}
    while openq:
        _, g, cur = heapq.heappop(openq)
        cx, cy, cl = cur
        if (cx, cy) == (gx, gy) and cl in goal_layers:
            path = [cur]
            while cur in came:
                cur = came[cur]
                path.append(cur)
            return path[::-1]
        if g > best.get(cur, 1e18):
            continue
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1),
                       (1, 1), (1, -1), (-1, 1), (-1, -1)):
            nx, ny = cx + dx, cy + dy
            if not (0 <= nx < W and 0 <= ny < H):
                continue
            atgoal = (nx, ny) == (gx, gy)
            if not atgoal:
                if not free(cl, nx, ny):
                    continue
                if dx and dy and not (free(cl, cx + dx, cy) and free(cl, cx, cy + dy)):
                    continue
            ng = g + GRID * (1.414 if dx and dy else 1.0)
            nn = (nx, ny, cl)
            if ng < best.get(nn, 1e18):
                best[nn] = ng
                came[nn] = cur
                heapq.heappush(openq, (ng + (abs(nx - gx) + abs(ny - gy)) * GRID, ng, nn))
        other = LB if cl == LF else LF
        if free(other, cx, cy) or (cx, cy) == (gx, gy):
            nn = (cx, cy, other)
            ng = g + VIA_COST * GRID
            if ng < best.get(nn, 1e18):
                best[nn] = ng
                came[nn] = cur
                heapq.heappush(openq, (ng + (abs(cx - gx) + abs(cy - gy)) * GRID, ng, nn))
    return None


def net_layer_at(board, xy):
    """(Netzcode, [Lagen]) eines Pads/einer Bahn/eines Vias an Position xy (mm)."""
    P = pcbnew.VECTOR2I(pcbnew.FromMM(xy[0]), pcbnew.FromMM(xy[1]))
    for f in board.GetFootprints():
        for pad in f.Pads():
            if pad.HitTest(P):
                lays = [l for l in (pcbnew.F_Cu, pcbnew.B_Cu) if pad.IsOnLayer(l)]
                return pad.GetNetCode(), lays
    for t in board.GetTracks():
        if t.HitTest(P):
            if t.GetClass() == "PCB_VIA":
                return t.GetNetCode(), [pcbnew.F_Cu, pcbnew.B_Cu]
            return t.GetNetCode(), [t.GetLayer()]
    return -1, []


def commit(board, nc, path, x0, y0, exact_a, exact_b):
    # Rasterpunkte in Weltkoordinaten, Endpunkte exakt auf die Pad-Mitten setzen
    wpts = [[x0 + cx * GRID, y0 + cy * GRID, cl] for (cx, cy, cl) in path]
    wpts[0][0], wpts[0][1] = exact_a
    wpts[-1][0], wpts[-1][1] = exact_b
    def track(x1, y1, x2, y2, layer):
        if (round(x1, 4), round(y1, 4)) == (round(x2, 4), round(y2, 4)):
            return
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(x1), pcbnew.FromMM(y1)))
        t.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(x2), pcbnew.FromMM(y2)))
        t.SetWidth(pcbnew.FromMM(TRACK_W))
        t.SetLayer(layer)
        t.SetNetCode(nc)
        board.Add(t)

    def via(x, y):
        v = pcbnew.PCB_VIA(board)
        v.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
        v.SetDrill(pcbnew.FromMM(VIA_DRILL))
        v.SetWidth(pcbnew.FromMM(VIA_D))
        v.SetNetCode(nc)
        v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        board.Add(v)

    prev = None
    for wx, wy, cl in wpts:
        if prev is not None:
            pwx, pwy, pl = prev
            if pl != cl:
                via(pwx, pwy)
                track(pwx, pwy, wx, wy, cl)
            else:
                track(pwx, pwy, wx, wy, cl)
        prev = (wx, wy, cl)


def finish(board) -> int:
    pairs = drc_unconnected(PCB)
    if not pairs:
        return 0
    bb = board.GetBoardEdgesBoundingBox()
    bbox = (pcbnew.ToMM(bb.GetLeft()) + 0.3, pcbnew.ToMM(bb.GetTop()) + 0.3,
            pcbnew.ToMM(bb.GetRight()) - 0.3, pcbnew.ToMM(bb.GetBottom()) - 0.3)
    done = 0
    for (a, la_hint), (b, lb_hint) in pairs:
        nca, la = net_layer_at(board, a)
        ncb, lb = net_layer_at(board, b)
        nc = nca if nca > 0 else ncb
        if nc <= 0:
            print(f"  offene Stelle bei {a} -- Netz nicht bestimmbar, "
                  f"von Hand ziehen")
            continue
        la = la_hint or la          # Lage aus der DRC-Beschreibung hat Vorrang
        lb = lb_hint or lb
        name = board.FindNet(nc).GetNetname()
        grids, W, H = block_grids(board, nc, bbox)
        path = astar(grids, W, H, a, b, bbox, la, lb)
        if path is None:
            print(f"  {name}: A* findet keinen Weg {a}->{b} -- von Hand ziehen")
            continue
        commit(board, nc, path, bbox[0], bbox[1], a, b)
        board.BuildConnectivity()
        done += 1
        print(f"  {name}: Luecke im Raster geschlossen ({len(path)} Punkte)")
    return done


def main() -> int:
    board = pcbnew.LoadBoard(str(PCB))
    n = finish(board)
    if n:
        pcbnew.SaveBoard(str(PCB), board)
        print(f"{n} Verbindung(en) ergaenzt.")
    else:
        print("nichts offen.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
