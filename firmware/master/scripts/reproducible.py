# Reproduzierbarer Master-Build (#43): absolute Pfade (Repository inkl. der
# geteilten lib/protocol aus firmware/module, PlatformIO-Pakete) gelangen sonst
# ueber __FILE__ und die Debug-Infos ins ELF; dessen SHA-256 steht im
# App-Deskriptor des Images. Mit diesen Abbildungen sind zwei Builds aus
# verschiedenen Verzeichnissen bytegleich.
import os

Import("env")  # noqa: F821  (SCons)

repo = os.path.dirname(os.path.dirname(env.subst("$PROJECT_DIR")))  # noqa: F821
packages = env.subst("$PROJECT_PACKAGES_DIR")  # noqa: F821
# __DATE__/__TIME__ in Bibliotheken (WiFiManager "aboutdate") fest auf 1970:
# GCC nimmt dafuer SOURCE_DATE_EPOCH.
env["ENV"]["SOURCE_DATE_EPOCH"] = "0"  # noqa: F821
env.Append(CCFLAGS=[  # noqa: F821
    "-ffile-prefix-map=%s=." % repo,
    "-ffile-prefix-map=%s=PIO" % packages,
])
