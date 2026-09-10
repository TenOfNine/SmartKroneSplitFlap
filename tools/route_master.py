#!/usr/bin/env python3
"""Routet hardware/master/master.kicad_pcb mit FreeRouting und
fuellt anschliessend die Masseflaechen.

    /usr/bin/python3 tools/route_master.py [--passes N] [--dry-run] [--no-zones]

Ablauf:
  1. Netzklassen aus gen_master_pcb.py in die .kicad_pro schreiben
     (alle Signale 0,5 mm, Versorgung +5V/+3V3/+15V/ADER9 0,8 mm, kein AC).
  2. Specctra-.dsn aus der .kicad_pcb exportieren (pcbnew).
  3. FreeRouting headless laufen lassen  (tools/vendor/, siehe setup_freerouting.sh).
  4. .ses zurueck in die .kicad_pcb importieren.
  5. tools/finish_routes.py schliesst die von FreeRouting offen gelassenen Reste.
  6. GND-Zone auf F.Cu und B.Cu anlegen und fuellen, Stitching-Vias.
  7. Netzklassen erneut in die .kicad_pro schreiben (SaveBoard setzt sie zurueck).

Die Platine bleibt zweilagig (F.Cu / B.Cu). Vor dem ersten Lauf:
bash tools/setup_freerouting.sh

Achtung: ueberschreibt die Leiterbahnen in der .kicad_pcb. Fuer den finalen
Planungslauf gedacht, nachdem die Bauteilpositionen feststehen. Der Antennen-
Keepout unter U1 (Modul-Footprint) haelt die Masseflaeche dort selbsttaetig frei.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    import pcbnew
except ImportError:
    sys.exit("pcbnew nicht gefunden -- mit /usr/bin/python3 starten.")

REPO = Path(__file__).resolve().parent.parent
PCB = REPO / "hardware" / "master" / "master.kicad_pcb"
VENDOR = REPO / "tools" / "vendor"

sys.path.insert(0, str(REPO / "tools"))
import gen_master_pcb as gpcb  # noqa: E402  (Netzklassen + patch)

# Rand der Masseflaeche zum Platinenumriss.
ZONE_EDGE_CLEARANCE_MM = 0.4


def java_bin() -> str:
    jp = VENDOR / "java-path"
    if jp.is_file():
        return jp.read_text().strip()
    return "java"


def _cpu_seconds(pid: int) -> float:
    try:
        with open(f"/proc/{pid}/stat") as f:
            parts = f.read().split()
        clk = 100.0  # os.sysconf('SC_CLK_TCK'), praktisch immer 100
        return (int(parts[13]) + int(parts[14])) / clk
    except (OSError, ValueError, IndexError):
        return -1.0


def _run_freerouting(cmd: list, ses: Path, hard_timeout: int = 300) -> int:
    """FreeRouting starten und die fertige .ses einsammeln.

    FreeRouting 2.3.0 (unter xvfb/JRE 25) beendet sich nach dem Routing haeufig
    nicht -- ein Thread haelt die JVM am Leben. Strategie:
      * .ses existiert und ~6 s groessenstabil        -> fertig, Prozess killen
      * Prozess lebt, verbraucht aber ~40 s keine CPU -> haengt: killen, .ses
        pruefen (Erfolg nur, wenn schon geschrieben)
      * Prozess beendet sich selbst / Zeitlimit       -> rc weiterreichen
    Rueckgabe 0 = Erfolg (nutzbare .ses), sonst != 0 (Aufrufer wiederholt).
    """
    import time

    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    start = time.monotonic()
    last_size, size_stable_since = -1, None
    last_cpu, cpu_flat_since = -1.0, None
    while True:
        rc = proc.poll()
        if rc is not None:
            return 0 if (rc == 0 and ses.is_file()) else (rc or 1)

        if ses.is_file():
            size = ses.stat().st_size
            if size > 0 and size == last_size:
                if size_stable_since is None:
                    size_stable_since = time.monotonic()
                elif time.monotonic() - size_stable_since > 6:
                    _kill(proc)
                    print("  FreeRouting: .ses stabil -> Prozess beendet")
                    return 0
            else:
                last_size, size_stable_since = size, None

        cpu = _cpu_seconds(proc.pid)
        if cpu >= 0 and abs(cpu - last_cpu) < 0.2:
            if cpu_flat_since is None:
                cpu_flat_since = time.monotonic()
            elif time.monotonic() - cpu_flat_since > 40:
                _kill(proc)
                ok = ses.is_file() and ses.stat().st_size > 0
                print(f"  FreeRouting: haengt ohne CPU-Last -> beendet ({'ses da' if ok else 'keine ses'})")
                return 0 if ok else 1
        else:
            last_cpu, cpu_flat_since = cpu, None

        if time.monotonic() - start > hard_timeout:
            _kill(proc)
            return 1
        time.sleep(2)


def _kill(proc) -> None:
    proc.terminate()
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()


def board_outline_points(board: "pcbnew.BOARD") -> list[tuple[int, int]]:
    """Rechteck-Umriss der Platine aus der Bounding-Box der Edge.Cuts."""
    bb = board.GetBoardEdgesBoundingBox()
    return [
        (bb.GetLeft(), bb.GetTop()), (bb.GetRight(), bb.GetTop()),
        (bb.GetRight(), bb.GetBottom()), (bb.GetLeft(), bb.GetBottom()),
    ]


def add_ground_zones(board: "pcbnew.BOARD") -> None:
    """GND-Zone auf F.Cu und B.Cu ueber die gesamte Platine."""
    gnd = board.FindNet("GND")
    if gnd is None:
        gnd = board.FindNet("/GND")
    if gnd is None:
        print("[!] Netz GND nicht gefunden -- keine Masseflaeche")
        return
    netcode = gnd.GetNetCode()
    pts = board_outline_points(board)

    # Bei kleinen SMD-Pads auf der Massefläche reicht 1 Wärmefallen-Speiche.
    try:
        board.GetDesignSettings().m_MinResolvedSpokes = 1
    except AttributeError:
        pass

    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        zone = pcbnew.ZONE(board)
        zone.SetLayer(layer)
        zone.SetNetCode(netcode)
        zone.SetIsFilled(True)
        zone.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        zone.SetLocalClearance(pcbnew.FromMM(0.3))
        zone.SetMinThickness(pcbnew.FromMM(0.25))
        zone.SetAssignedPriority(0)
        # isolierte Kupferinseln ohne GND-Anbindung entfernen
        zone.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        outline = zone.Outline()
        outline.NewOutline()
        for x, y in pts:
            outline.Append(x, y)
        zone.SetZoneName(f"GND_{pcbnew.LayerName(layer)}")
        board.Add(zone)

    # THT-Steckerpads (J1..J6) sollen massiv an die Massefläche -- bei
    # randnahen Pads bildet die Wärmefalle sonst nur eine Speiche (starved
    # thermal) und der Strompfad für die Ketten-/AC-Rückleitung wird dünn.
    solid = 0
    for f in board.GetFootprints():
        if not f.GetReference().startswith("J"):
            continue
        for pad in f.Pads():
            if pad.GetNetCode() == netcode:
                pad.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
                solid += 1
    if solid:
        print(f"{solid} Stecker-GND-Pads massiv an die Massefläche")

    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())

    # Vias entfernen, die nach dem Füllen nicht auf beiden Lagen Kupfer sehen
    # (via_dangling): FreeRouting-Reste ohne B.Cu-Seite und Stitching-Vias, die
    # in einer Aussparung der Massefläche gelandet sind.
    def _cu(x, y, layer, net):
        for t in board.GetTracks():
            if t.GetClass() != "PCB_TRACK" or t.GetLayer() != layer:
                continue
            for pt in (t.GetStart(), t.GetEnd()):
                if abs(pcbnew.ToMM(pt.x) - x) < 0.05 and abs(pcbnew.ToMM(pt.y) - y) < 0.05:
                    return True
        for f in board.GetFootprints():
            for pad in f.Pads():
                if pad.GetNetCode() == net and pad.IsOnLayer(layer):
                    pp = pad.GetPosition()
                    if abs(pcbnew.ToMM(pp.x) - x) < 0.05 and abs(pcbnew.ToMM(pp.y) - y) < 0.05:
                        return True
        for z in board.Zones():
            if z.GetNetCode() == net and z.IsOnLayer(layer):
                if z.HitTestFilledArea(layer, pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))):
                    return True
        return False

    dropped = 0
    for v in list(board.GetTracks()):
        if v.GetClass() != "PCB_VIA":
            continue
        p = v.GetPosition()
        x, y, nc = pcbnew.ToMM(p.x), pcbnew.ToMM(p.y), v.GetNetCode()
        if not (_cu(x, y, pcbnew.F_Cu, nc) and _cu(x, y, pcbnew.B_Cu, nc)):
            board.Delete(v)
            dropped += 1
    if dropped:
        board.BuildConnectivity()
        filler.Fill(board.Zones())
        print(f"{dropped} freistehende Vias entfernt")
    print(f"GND-Zonen gefuellt (F.Cu + B.Cu, {len(list(board.Zones()))} Zonen)")


def _pt_seg_mm(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L2))
    cx, cy = ax + t * dx, ay + t * dy
    return ((px - cx) ** 2 + (py - cy) ** 2) ** 0.5


def add_stitching_vias(board: "pcbnew.BOARD", pitch_mm: float = 5.0) -> None:
    """GND-Vias im Raster, die F.Cu- und B.Cu-Massefläche verbinden. Kandidaten
    zu nahe an Bahnen, Pads, Vias oder Bohrungen werden übersprungen."""
    gnd = board.FindNet("GND") or board.FindNet("/GND")
    netcode = gnd.GetNetCode()
    bb = board.GetBoardEdgesBoundingBox()
    x0, y0 = pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop())
    x1, y1 = pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom())

    VIA_R = 0.4          # halber Via-Durchmesser
    CLR = 0.35           # Sicherheitsabstand
    segs, circles = [], []
    for t in board.GetTracks():
        if t.GetClass() == "PCB_VIA":
            p = t.GetPosition()
            # jedes vorhandene Via ist eine Bohrung -> Mindest-Bohrabstand halten
            circles.append((pcbnew.ToMM(p.x), pcbnew.ToMM(p.y), 0.45))
            continue
        if t.GetNetCode() == netcode:
            continue
        s, e = t.GetStart(), t.GetEnd()
        hw = pcbnew.ToMM(t.GetWidth()) / 2
        segs.append((pcbnew.ToMM(s.x), pcbnew.ToMM(s.y),
                     pcbnew.ToMM(e.x), pcbnew.ToMM(e.y), hw))
    for f in board.GetFootprints():
        for pad in f.Pads():
            p = pad.GetPosition()
            keep_out = pcbnew.ToMM(pad.GetBoundingRadius())
            if pad.GetNetCode() != netcode:
                circles.append((pcbnew.ToMM(p.x), pcbnew.ToMM(p.y), keep_out))
            else:
                circles.append((pcbnew.ToMM(p.x), pcbnew.ToMM(p.y), keep_out * 0.5))
        for hole in ("H",):
            pass
    # Bohrungen (MountingHole) grob über die Footprints mit Ref H*
    for f in board.GetFootprints():
        if f.GetReference().startswith("H"):
            p = f.GetPosition()
            circles.append((pcbnew.ToMM(p.x), pcbnew.ToMM(p.y), 3.0))

    def ok(x, y):
        for ax, ay, ex, ey, hw in segs:
            if _pt_seg_mm(x, y, ax, ay, ex, ey) < VIA_R + hw + CLR:
                return False
        for cx, cy, r in circles:
            if ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5 < VIA_R + r + CLR:
                return False
        return True

    def drop_via(x, y):
        v = pcbnew.PCB_VIA(board)
        v.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
        v.SetDrill(pcbnew.FromMM(0.4))
        v.SetWidth(pcbnew.FromMM(0.8))
        v.SetNetCode(netcode)
        v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        board.Add(v)
        circles.append((x, y, 0.45))  # als Bohrung für spätere ok()-Prüfungen

    placed = 0
    y = y0 + pitch_mm
    while y < y1 - 3.0:
        x = x0 + pitch_mm
        while x < x1 - 3.0:
            if ok(x, y):
                drop_via(x, y)
                placed += 1
            x += pitch_mm
        y += pitch_mm

    # zusätzlich neben jedes GND-SMD-Pad ein Via, damit jedes GND-Pad die
    # durchgehende B.Cu-Fläche erreicht (verhindert eingeschnürte F.Cu-Inseln).
    fanout = 0
    for f in board.GetFootprints():
        for pad in f.Pads():
            if pad.GetNetCode() != netcode:
                continue
            if pad.GetAttribute() not in (pcbnew.PAD_ATTRIB_SMD, pcbnew.PAD_ATTRIB_CONN):
                continue
            pp = pad.GetPosition()
            cx, cy = pcbnew.ToMM(pp.x), pcbnew.ToMM(pp.y)
            for dx, dy in ((0, 1.4), (0, -1.4), (1.4, 0), (-1.4, 0),
                           (0, 1.8), (0, -1.8), (1.8, 0), (-1.8, 0)):
                if ok(cx + dx, cy + dy):
                    drop_via(cx + dx, cy + dy)
                    fanout += 1
                    break

    board.BuildConnectivity()
    print(f"{placed} GND-Stitching-Vias (Raster {pitch_mm:g} mm) + {fanout} an GND-Pads")


def run_drc() -> None:
    from shutil import which
    cli = ["kicad-cli"]
    if which("xvfb-run"):
        cli = ["xvfb-run", "-a", *cli]
    rep = REPO / "docs" / "drc-master.rpt"
    r = subprocess.run([*cli, "pcb", "drc", "--exit-code-violations",
                        "--severity-error", "-o", str(rep), str(PCB)])
    print(f"DRC: {'keine Fehler' if r.returncode == 0 else f'Verstoesse -> {rep.name}'}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--passes", type=int, default=6,
                    help="Obergrenze der FreeRouting-Auto-Routing-Durchlaeufe "
                         "(Vorgabe 6; konvergiert i. d. R. in <6 Passes. Hohe "
                         "Werte verlaengern nur die Optimierung und provozieren "
                         "einen JVM-Haenger nach dem Schreiben der .ses)")
    ap.add_argument("--dry-run", action="store_true",
                    help="nur .dsn/.ses erzeugen, nicht importieren")
    ap.add_argument("--no-zones", action="store_true",
                    help="keine Masseflaechen anlegen")
    args = ap.parse_args()

    jar = next(VENDOR.glob("freerouting-*.jar"), None)
    if jar is None:
        sys.exit("FreeRouting fehlt -- bash tools/setup_freerouting.sh")

    gpcb.patch_project_netclasses()
    print("Netzklassen in die .kicad_pro geschrieben")

    board = pcbnew.LoadBoard(str(PCB))

    # Frischer Lauf: vorhandene Leiterbahnen, Vias und Zonen entfernen.
    old = list(board.GetTracks()) + list(board.Zones())
    if old:
        for item in old:
            board.Delete(item)
        board.BuildConnectivity()
        print(f"vorherige Verdrahtung entfernt ({len(old)} Objekte)")

    with tempfile.TemporaryDirectory() as td:
        dsn = Path(td) / "board.dsn"
        ses = Path(td) / "board.ses"

        if not pcbnew.ExportSpecctraDSN(board, str(dsn)):
            sys.exit("Specctra-DSN-Export fehlgeschlagen")
        print(f"DSN exportiert ({dsn.stat().st_size} B)")

        # -Dfreerouting.gui.enabled=false haelt FreeRouting 2.3.0 headless.
        # Trotzdem beendet sich die JVM nach dem Schreiben der .ses gelegentlich
        # nicht (haengender Executor/AWT-Thread). Darum: als Popen starten, auf
        # eine stabile .ses warten und den Prozess dann selbst beenden.
        cmd = [java_bin(), "-Dfreerouting.gui.enabled=false",
               "-jar", str(jar), "-de", str(dsn), "-do", str(ses),
               "-mp", str(args.passes), "-l", "en"]
        print("  $", " ".join(cmd))
        # FreeRouting 2.3.0 ist hier nicht deterministisch -- bis zu 3 Anlaeufe.
        for attempt in range(1, 4):
            rc = _run_freerouting(cmd, ses)
            if rc == 0 and ses.is_file() and ses.stat().st_size > 0:
                break
            ses.unlink(missing_ok=True)
            print(f"  FreeRouting Anlauf {attempt} erfolglos (rc={rc}), neuer Versuch ...")
        else:
            sys.exit("FreeRouting fehlgeschlagen (3 Anlaeufe)")

        if args.dry_run:
            keep = REPO / "hardware" / "daughtercard" / "daughtercard.ses"
            keep.write_bytes(ses.read_bytes())
            print(f"--dry-run: Session nach {keep.relative_to(REPO)} kopiert, kein Import")
            return 0

        if not pcbnew.ImportSpecctraSES(board, str(ses)):
            sys.exit("Specctra-SES-Import fehlgeschlagen")

    # Verbindungen, die FreeRouting offen laesst, mit dem Rastersuch-Router
    # schliessen (arbeitet auf der gespeicherten Datei -> hier zwischenspeichern).
    board.BuildConnectivity()
    pcbnew.SaveBoard(str(PCB), board)
    import finish_routes
    if finish_routes.finish(board, PCB):
        pcbnew.SaveBoard(str(PCB), board)
    board = pcbnew.LoadBoard(str(PCB))

    if not args.no_zones:
        # FreeRoutings GND-*Bahnen* verwerfen (die Flaeche + Stitching + je ein
        # Via an jedem GND-Pad verbinden GND vollstaendig). GND-*Vias* bleiben.
        gnet = board.FindNet("GND") or board.FindNet("/GND")
        if gnet is not None:
            gc = gnet.GetNetCode()
            drop = [t for t in board.GetTracks()
                    if t.GetNetCode() == gc and t.GetClass() == "PCB_TRACK"]
            for t in drop:
                board.Delete(t)
            if drop:
                board.BuildConnectivity()
                print(f"GND-Bahnen entfernt ({len(drop)}) -- Masse ueber Flaeche + Stitching")
        add_stitching_vias(board)
        add_ground_zones(board)

    board.BuildConnectivity()
    pcbnew.SaveBoard(str(PCB), board)
    gpcb.patch_project_netclasses()

    tracks = len(list(board.GetTracks()))
    print(f"geschrieben: {PCB.relative_to(REPO)}  ({tracks} Segmente/Vias)")

    # Maker-Kennzeichnung + schwarz/weiss-Lagenaufbau NACH dem Routen setzen
    # (SES-Import/SaveBoard verwerfen Silk-Texte und den Stackup-Textblock).
    subprocess.run([sys.executable, str(REPO / "tools" / "add_silk_marks.py"),
                    "--board", str(PCB), "--logo-pos", "46", "30",
                    "--text-pos", "46", "40"], check=False)

    run_drc()
    # Vorschau direkt aus der gerouteten Datei (gen_master_pcb.py --png wuerde
    # die Platine neu aufbauen und die Verdrahtung verwerfen!).
    try:
        gpcb.render_png()
    except Exception as exc:  # noqa: BLE001
        print(f"[!] Vorschau uebersprungen: {exc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
