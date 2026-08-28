#!/usr/bin/env bash
#
# Toolchain-Setup fuer die Entwicklungs-VM.
#
# Zielumgebung: Debian oder Ubuntu (siehe docs/toolchain.md).
# Das Skript installiert KiCad 9, eine Python-venv mit kicad-sch-api und
# PlatformIO und verifiziert anschliessend alle Versionen. Zusaetzlich
# prueft es, ob kicad-sch-api mit der installierten KiCad-Version
# zusammenarbeitet, und faellt sonst auf kicad-skip zurueck (Backlog T2).
#
# Das Skript ist idempotent und kann mehrfach ausgefuehrt werden.
#
# Aufruf:
#   bash tools/setup.sh                 # vollstaendige Einrichtung
#   bash tools/setup.sh --skip-apt      # nur Python-venv und Verifikation
#   bash tools/setup.sh --verify-only   # nichts installieren, nur pruefen
#
# Umgebungsvariablen:
#   KICAD_SOURCE=ppa|flatpak|system    Bezugsquelle fuer KiCad (Vorgabe: ppa)
#   PYTHON=python3                     zu verwendender Interpreter
#
set -euo pipefail

# ----------------------------------------------------------------------------
# 0. Rahmen
# ----------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VENV_DIR="${REPO_ROOT}/.venv"

KICAD_SOURCE="${KICAD_SOURCE:-ppa}"
PYTHON="${PYTHON:-python3}"

DO_APT=1
DO_PIP=1
DO_VERIFY=1
DO_KICAD_CFG_ONLY=0

for arg in "$@"; do
	case "${arg}" in
		--skip-apt)     DO_APT=0 ;;
		--verify-only)  DO_APT=0; DO_PIP=0 ;;
		--kicad-config) DO_APT=0; DO_PIP=0; DO_VERIFY=0; DO_KICAD_CFG_ONLY=1 ;;
		--help|-h)
			cat <<'USAGE'
Toolchain-Setup fuer die Entwicklungs-VM (Debian/Ubuntu, siehe docs/toolchain.md).

Installiert KiCad 9, eine Python-venv mit kicad-sch-api und PlatformIO und
verifiziert alle Versionen. Prueft zusaetzlich, ob kicad-sch-api mit der
installierten KiCad-Version zusammenarbeitet, und faellt sonst auf
kicad-skip zurueck (Backlog T2). Idempotent.

Aufruf:
  bash tools/setup.sh                 vollstaendige Einrichtung
  bash tools/setup.sh --skip-apt      nur Python-venv und Verifikation
  bash tools/setup.sh --verify-only   nichts installieren, nur pruefen
  bash tools/setup.sh --kicad-config  nur die KiCad-Bibliothekstabellen (CI)
  bash tools/setup.sh --help          diese Hilfe

Umgebungsvariablen:
  KICAD_SOURCE=ppa|flatpak|system     Bezugsquelle fuer KiCad (Vorgabe: ppa)
  PYTHON=python3                      zu verwendender Interpreter
USAGE
			exit 0
			;;
		*)
			echo "Unbekanntes Argument: ${arg}" >&2
			exit 2
			;;
	esac
done

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[!]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m[x]\033[0m %s\n' "$*" >&2; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }

# ----------------------------------------------------------------------------
# 1. Vorbedingungen
# ----------------------------------------------------------------------------

[ "$(uname -s)" = "Linux" ] || die "Das Skript ist fuer eine Linux-VM gedacht (siehe docs/toolchain.md)."

if [ "${DO_APT}" -eq 1 ]; then
	have apt-get || die "apt-get nicht gefunden. Erwartet wird Debian oder Ubuntu."
	have sudo    || die "sudo nicht gefunden."
fi

# ----------------------------------------------------------------------------
# 2. Systempakete
# ----------------------------------------------------------------------------

install_apt_packages() {
	log "Systempakete installieren"

	local base_pkgs=(
		git
		build-essential
		python3
		python3-venv
		python3-pip
		xvfb            # kicad-cli-Unterbefehle brauchen ein Display
		poppler-utils   # pdftoppm fuer die PNG-Vorschau (gen_daughtercard_sch.py --png)
	)

	sudo apt-get update -qq
	sudo apt-get install -y --no-install-recommends "${base_pkgs[@]}"

	case "${KICAD_SOURCE}" in
		ppa)
			if have lsb_release && [ "$(lsb_release -is)" = "Ubuntu" ]; then
				log "KiCad 9 aus dem offiziellen PPA"
				sudo apt-get install -y --no-install-recommends software-properties-common
				sudo add-apt-repository -y ppa:kicad/kicad-9.0-releases
				sudo apt-get update -qq
				sudo apt-get install -y --install-recommends kicad
			else
				warn "Kein Ubuntu erkannt. Das kicad-Paket der Distribution wird verwendet."
				warn "Ist es aelter als 9.0, KICAD_SOURCE=flatpak setzen (siehe docs/toolchain.md)."
				sudo apt-get install -y --install-recommends kicad
			fi
			;;
		flatpak)
			log "KiCad 9 als Flatpak"
			sudo apt-get install -y --no-install-recommends flatpak
			flatpak remote-add --if-not-exists flathub \
				https://flathub.org/repo/flathub.flatpakrepo
			flatpak install -y --noninteractive flathub org.kicad.KiCad
			;;
		system)
			log "KiCad-Paket der Distribution ohne zusaetzliche Quelle"
			sudo apt-get install -y --install-recommends kicad
			;;
		*)
			die "KICAD_SOURCE muss ppa, flatpak oder system sein (ist: ${KICAD_SOURCE})."
			;;
	esac
}

# kicad-cli aufrufbar machen, egal ob nativ oder Flatpak.
# Schreibt den Aufruf nach der globalen Variable KICAD_CLI.
resolve_kicad_cli() {
	if have kicad-cli; then
		KICAD_CLI=(kicad-cli)
	elif have flatpak && flatpak info org.kicad.KiCad >/dev/null 2>&1; then
		KICAD_CLI=(flatpak run --command=kicad-cli org.kicad.KiCad)
	else
		KICAD_CLI=()
	fi
}

if [ "${DO_APT}" -eq 1 ]; then
	install_apt_packages
fi

# ----------------------------------------------------------------------------
# 3. Python-Umgebung
# ----------------------------------------------------------------------------

setup_venv() {
	log "Python-venv unter ${VENV_DIR}"

	have "${PYTHON}" || die "${PYTHON} nicht gefunden."

	if [ ! -d "${VENV_DIR}" ]; then
		"${PYTHON}" -m venv "${VENV_DIR}"
	fi

	# shellcheck disable=SC1091
	source "${VENV_DIR}/bin/activate"

	python -m pip install --upgrade pip wheel

	# kicad-sch-api schreibt .kicad_sch formattreu ohne laufendes KiCad.
	# kicad-skip ist die in docs/toolchain.md genannte Rueckfallebene und
	# wird gleich mitinstalliert, damit der Umstieg keinen zweiten Lauf
	# erfordert.
	python -m pip install \
		"platformio" \
		"kicad-sch-api" \
		"kicad-skip" \
		"pillow"          # Zuschnitt der PNG-Vorschau
}

if [ "${DO_PIP}" -eq 1 ]; then
	setup_venv
else
	# Fuer --verify-only trotzdem die venv aktivieren, falls vorhanden.
	if [ -f "${VENV_DIR}/bin/activate" ]; then
		# shellcheck disable=SC1091
		source "${VENV_DIR}/bin/activate"
	fi
fi

# ----------------------------------------------------------------------------
# 3b. KiCad-Konfiguration fuer den Headless-Betrieb
# ----------------------------------------------------------------------------
#
# 'kicad-cli sch erc' loest Symbolbibliotheken ueber die globale sym-lib-table
# auf. Die legt sonst nur die GUI beim ersten Start an; headless fehlt sie und
# ERC meldet fuer jedes Bauteil "configuration does not include the symbol
# library". Dieselbe Logik gilt fuer Footprints (T5).

setup_kicad_config() {
	local cfg_base share_base
	cfg_base="${XDG_CONFIG_HOME:-${HOME}/.config}/kicad/9.0"

	share_base=""
	for cand in /usr/share/kicad /usr/local/share/kicad /app/share/kicad; do
		if [ -d "${cand}/template" ]; then share_base="${cand}"; break; fi
	done
	if [ -z "${share_base}" ]; then
		warn "KiCad-Datenverzeichnis nicht gefunden, ueberspringe Bibliothekstabellen."
		return
	fi

	log "KiCad-Bibliothekstabellen unter ${cfg_base}"
	mkdir -p "${cfg_base}"
	for tbl in sym-lib-table fp-lib-table; do
		if [ ! -f "${cfg_base}/${tbl}" ] && [ -f "${share_base}/template/${tbl}" ]; then
			cp "${share_base}/template/${tbl}" "${cfg_base}/${tbl}"
			log "  ${tbl} aus der Vorlage kopiert"
		fi
	done

	# Die Tabellen referenzieren ${KICAD9_SYMBOL_DIR} usw. Fehlen die Variablen,
	# in kicad_common.json eintragen, damit auch nicht-interaktive Aufrufe sie
	# aufloesen.
	python - "${cfg_base}/kicad_common.json" "${share_base}" <<'PY'
import json, os, sys

path, share = sys.argv[1], sys.argv[2]
try:
    with open(path) as fh:
        cfg = json.load(fh)
except (OSError, ValueError):
    cfg = {}

env = cfg.setdefault("environment", {})
vars_ = env.get("vars") or {}
defaults = {
    "KICAD9_SYMBOL_DIR": f"{share}/symbols",
    "KICAD9_FOOTPRINT_DIR": f"{share}/footprints",
    "KICAD9_3DMODEL_DIR": f"{share}/3dmodels",
    "KICAD9_TEMPLATE_DIR": f"{share}/template",
}
changed = False
for k, v in defaults.items():
    if k not in vars_ and os.path.isdir(v):
        vars_[k] = v
        changed = True
env["vars"] = vars_

if changed:
    with open(path, "w") as fh:
        json.dump(cfg, fh, indent=2)
    print("  kicad_common.json: Bibliothekspfade ergaenzt")
PY
}

if [ "${DO_APT}" -eq 1 ] || [ "${DO_PIP}" -eq 1 ] || [ "${DO_KICAD_CFG_ONLY}" -eq 1 ]; then
	setup_kicad_config
fi

if [ "${DO_KICAD_CFG_ONLY}" -eq 1 ]; then
	log "KiCad-Bibliothekstabellen eingerichtet."
	exit 0
fi

# ----------------------------------------------------------------------------
# 4. Verifikation
# ----------------------------------------------------------------------------

VERIFY_FAIL=0
KICAD_VERSION="(nicht gefunden)"
PIO_VERSION="(nicht gefunden)"
SCH_API_VERSION="(nicht installiert)"
SKIP_VERSION="(nicht installiert)"
SCH_BACKEND="unbestimmt"

verify_kicad() {
	resolve_kicad_cli
	if [ "${#KICAD_CLI[@]}" -eq 0 ]; then
		warn "kicad-cli nicht gefunden."
		VERIFY_FAIL=1
		return
	fi
	# 'version' braucht kein Display.
	if KICAD_VERSION="$("${KICAD_CLI[@]}" version 2>/dev/null)"; then
		log "kicad-cli: ${KICAD_VERSION}"
	else
		warn "kicad-cli lieferte keine Version."
		KICAD_VERSION="(Fehler)"
		VERIFY_FAIL=1
	fi
}

verify_pio() {
	if have pio && PIO_VERSION="$(pio --version 2>/dev/null)"; then
		log "PlatformIO: ${PIO_VERSION}"
	else
		warn "pio nicht gefunden. Wurde die venv aktiviert?"
		VERIFY_FAIL=1
	fi
}

verify_python_libs() {
	SCH_API_VERSION="$(python -c 'import importlib.metadata as m; print(m.version("kicad-sch-api"))' 2>/dev/null || echo '(nicht installiert)')"
	SKIP_VERSION="$(python -c 'import importlib.metadata as m; print(m.version("kicad-skip"))' 2>/dev/null || echo '(nicht installiert)')"
	log "kicad-sch-api: ${SCH_API_VERSION}"
	log "kicad-skip:    ${SKIP_VERSION}"
}

# Prueft, ob eine mit kicad-sch-api erzeugte Datei von der installierten
# KiCad-Version fehlerfrei gelesen wird. Das ist die in Backlog T2
# geforderte Verifikation. kicad-sch-api dokumentiert offiziell nur
# KiCad 7 und 8; ob KiCad 9 die erzeugten Dateien akzeptiert, laesst sich
# nur durch einen echten Lese-Versuch feststellen.
verify_sch_api_roundtrip() {
	if [ "${SCH_API_VERSION}" = "(nicht installiert)" ]; then
		warn "kicad-sch-api nicht installiert, Roundtrip-Test uebersprungen."
		SCH_BACKEND="kicad-skip"
		return
	fi
	resolve_kicad_cli
	if [ "${#KICAD_CLI[@]}" -eq 0 ]; then
		warn "Ohne kicad-cli kein Roundtrip-Test moeglich."
		SCH_BACKEND="unbestimmt"
		return
	fi

	local workdir sch
	workdir="$(mktemp -d)"
	# shellcheck disable=SC2064
	trap "rm -rf '${workdir}'" RETURN
	sch="${workdir}/roundtrip.kicad_sch"

	log "Roundtrip-Test kicad-sch-api -> KiCad ${KICAD_VERSION}"

	if ! python - "${sch}" <<'PY'
import sys

try:
    import kicad_sch_api as ksa
except Exception as exc:  # pragma: no cover - Diagnose
    print(f"import kicad_sch_api fehlgeschlagen: {exc}", file=sys.stderr)
    sys.exit(3)

out = sys.argv[1]

# Moeglichst kleine, aber gueltige Schaltung erzeugen. Die API-Oberflaeche
# hat sich zwischen den 0.x-Versionen geaendert, daher defensiv aufrufen.
sch = ksa.create_schematic() if hasattr(ksa, "create_schematic") else ksa.Schematic.create()

# Ab kicad-sch-api 0.5.x werden Bauteile ueber sch.components.add(lib_id, ...)
# angelegt. Aeltere 0.x-Namen (add_component/add_symbol) werden als Rueckfall
# weiter mitgeprueft, damit das Skript auch mit einer aelteren Bibliothek laeuft.
added = False
candidates = []
if hasattr(sch, "components"):
    candidates += [getattr(sch.components, n, None) for n in ("add", "add_component", "add_symbol")]
candidates += [getattr(sch, n, None) for n in ("add_component", "add_symbol")]

for fn in candidates:
    if not callable(fn):
        continue
    try:
        fn("Device:R", reference="R1", value="10k", position=(50, 50))
        added = True
        break
    except TypeError:
        try:
            fn(lib_id="Device:R", reference="R1", value="10k", position=(50, 50))
            added = True
            break
        except Exception:
            pass
    except Exception:
        pass

if not added:
    print("Konnte kein Bauteil hinzufuegen, API-Signatur unbekannt.", file=sys.stderr)
    sys.exit(4)

(sch.save if hasattr(sch, "save") else sch.write)(out)
print(f"geschrieben: {out}")
PY
	then
		warn "kicad-sch-api konnte keine Testdatei erzeugen."
		SCH_BACKEND="kicad-skip"
		VERIFY_FAIL=1
		return
	fi

	# KiCad die Datei lesen lassen. ERC ist der strengste zerstoerungsfreie
	# Test; er braucht ein Display, daher xvfb-run.
	local erc_log="${workdir}/erc.txt"
	local runner=(); have xvfb-run && runner=(xvfb-run -a)
	if "${runner[@]}" "${KICAD_CLI[@]}" sch erc \
			--exit-code-violations \
			-o "${erc_log}" "${sch}" >"${workdir}/erc.stdout" 2>&1 \
		|| grep -qiE 'violation|warning|error count' "${erc_log}" 2>/dev/null
	then
		# ERC lief durch (Verletzungen sind erlaubt, es geht nur ums Lesen).
		if grep -qiE 'unknown|unexpected|expecting|io error|could not load|version' "${workdir}/erc.stdout"; then
			warn "KiCad meldete ein Formatproblem beim Lesen der Datei:"
			sed 's/^/    /' "${workdir}/erc.stdout" >&2
			SCH_BACKEND="kicad-skip"
			VERIFY_FAIL=1
		else
			log "KiCad hat die von kicad-sch-api erzeugte Datei gelesen."
			SCH_BACKEND="kicad-sch-api"
		fi
	else
		warn "kicad-cli sch erc konnte die Datei nicht verarbeiten:"
		sed 's/^/    /' "${workdir}/erc.stdout" >&2
		SCH_BACKEND="kicad-skip"
		VERIFY_FAIL=1
	fi
}

if [ "${DO_VERIFY}" -eq 1 ]; then
	log "Verifikation"
	verify_kicad
	verify_pio
	verify_python_libs
	verify_sch_api_roundtrip

	echo
	echo "  ┌─────────────────────────────────────────────────────────────"
	printf "  │ %-22s %s\n" "kicad-cli"        "${KICAD_VERSION}"
	printf "  │ %-22s %s\n" "PlatformIO"       "${PIO_VERSION}"
	printf "  │ %-22s %s\n" "kicad-sch-api"    "${SCH_API_VERSION}"
	printf "  │ %-22s %s\n" "kicad-skip"       "${SKIP_VERSION}"
	printf "  │ %-22s %s\n" "Schaltplan-Backend" "${SCH_BACKEND}"
	printf "  │ %-22s %s\n" "venv"             "${VENV_DIR}"
	echo "  └─────────────────────────────────────────────────────────────"
	echo
	echo "  Aktivierung fuer die weitere Arbeit:"
	echo "      source ${VENV_DIR#"${REPO_ROOT}/"}/bin/activate"
	echo

	if [ "${SCH_BACKEND}" = "kicad-skip" ]; then
		warn "kicad-sch-api arbeitet mit dieser KiCad-Version nicht zuverlaessig."
		warn "Fuer T4 wird kicad-skip verwendet. Alternativ eine KiCad-8-Instanz"
		warn "parallel installieren (siehe docs/toolchain.md, Abschnitt 1)."
	fi

	if [ "${VERIFY_FAIL}" -ne 0 ]; then
		die "Verifikation unvollstaendig. Meldungen oben pruefen."
	fi
	log "Toolchain vollstaendig und verifiziert."
fi
