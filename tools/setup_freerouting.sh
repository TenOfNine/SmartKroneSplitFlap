#!/usr/bin/env bash
#
# Laedt FreeRouting und -- falls noetig -- eine passende JRE nach tools/vendor/.
# tools/vendor/ ist in .gitignore. FreeRouting 2.3.0 braucht Java 25.
#
#   bash tools/setup_freerouting.sh
#
# Danach routet tools/route_daughtercard.py die Platine (nach der finalen
# Bauteilplatzierung).
set -euo pipefail

FR_VERSION="2.3.0"
JRE_VERSION="25"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENDOR="${SCRIPT_DIR}/vendor"
mkdir -p "${VENDOR}"

JAR="${VENDOR}/freerouting-${FR_VERSION}.jar"
if [ ! -f "${JAR}" ]; then
	echo "==> FreeRouting ${FR_VERSION}"
	curl -fsSL -o "${JAR}" \
		"https://github.com/freerouting/freerouting/releases/download/v${FR_VERSION}/freerouting-${FR_VERSION}.jar"
fi

# Java >= 25 finden oder Temurin-JRE herunterladen.
have_java25() {
	local j="$1"
	command -v "$j" >/dev/null 2>&1 || return 1
	local v
	v="$("$j" -version 2>&1 | head -1 | grep -oE '"[0-9]+' | tr -d '"')"
	[ -n "$v" ] && [ "$v" -ge "${JRE_VERSION}" ]
}

if have_java25 java; then
	echo "==> System-Java >= ${JRE_VERSION} vorhanden"
	echo "java" > "${VENDOR}/java-path"
elif [ -x "${VENDOR}/jre${JRE_VERSION}/bin/java" ]; then
	echo "==> JRE ${JRE_VERSION} bereits in tools/vendor/"
	echo "${VENDOR}/jre${JRE_VERSION}/bin/java" > "${VENDOR}/java-path"
else
	echo "==> Temurin JRE ${JRE_VERSION} (headless)"
	arch="$(uname -m)"; case "${arch}" in x86_64) arch=x64 ;; aarch64) arch=aarch64 ;; esac
	url="$(curl -fsSL "https://api.adoptium.net/v3/assets/latest/${JRE_VERSION}/hotspot?architecture=${arch}&image_type=jre&os=linux&vendor=eclipse" \
		| python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["binary"]["package"]["link"])')"
	curl -fsSL -o "${VENDOR}/jre.tar.gz" "${url}"
	tar -xzf "${VENDOR}/jre.tar.gz" -C "${VENDOR}"
	rm "${VENDOR}/jre.tar.gz"
	mv "${VENDOR}"/jdk-${JRE_VERSION}* "${VENDOR}/jre${JRE_VERSION}"
	echo "${VENDOR}/jre${JRE_VERSION}/bin/java" > "${VENDOR}/java-path"
fi

echo
"$(cat "${VENDOR}/java-path")" -jar "${JAR}" --version 2>&1 | head -1 || true
echo "FreeRouting eingerichtet. Router-Lauf: /usr/bin/python3 tools/route_daughtercard.py"
