#!/usr/bin/env bash
# Checks that the Linux release binaries run on the Ubuntu 22.04 baseline (glibc 2.35): no
# unresolved libraries, no newer GLIBC symbol versions, and no libstdc++/ALSA symbols the
# linked system libraries don't provide. Works for x86_64 and aarch64 (the VST3 binary
# folder is named after the machine architecture).
#
# Usage: audit-linux-abi.sh <artefacts-root>   (e.g. build/Intersect_artefacts/Release)

set -euo pipefail

ART_ROOT="${1:?usage: audit-linux-abi.sh <artefacts-root>}"
ARCH="$(uname -m)"
STANDALONE="$ART_ROOT/Standalone/INTERSECT"
VST3="$ART_ROOT/VST3/INTERSECT.vst3/Contents/${ARCH}-linux/INTERSECT.so"
MAX_GLIBC="2.35"

ldd --version

find_linked_library() {
  local binary="$1"
  local soname="$2"
  ldd "$binary" | awk -v soname="$soname" '$1 == soname { print $3; exit }'
}

fail_newer_glibc() {
  local binary="$1"
  local bad_versions=""
  local version

  while IFS= read -r version; do
    if [ "$(printf '%s\n%s\n' "$MAX_GLIBC" "$version" | sort -V | tail -n 1)" != "$MAX_GLIBC" ]; then
      bad_versions="${bad_versions}GLIBC_${version}"$'\n'
    fi
  done < <(readelf --version-info "$binary" | grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' | sed 's/^GLIBC_//' | sort -Vu)

  if [ -n "$bad_versions" ]; then
    echo "::error file=$binary::Requires glibc newer than GLIBC_$MAX_GLIBC"
    printf '%s' "$bad_versions"
    return 1
  fi
}

fail_unavailable_symbols() {
  local binary="$1"
  local soname="$2"
  local pattern="$3"
  local provider
  local symbols
  local missing=""

  symbols=$(readelf --version-info "$binary" | grep -oE "$pattern" | sort -Vu || true)
  if [ -z "$symbols" ]; then
    return 0
  fi

  provider=$(find_linked_library "$binary" "$soname")
  if [ -z "$provider" ] || [ ! -f "$provider" ]; then
    echo "::error file=$binary::Could not locate $soname via ldd"
    return 1
  fi

  while IFS= read -r symbol; do
    if ! grep -Fxq "$symbol" < <(strings "$provider"); then
      missing="${missing}${symbol}"$'\n'
    fi
  done <<< "$symbols"

  if [ -n "$missing" ]; then
    echo "::error file=$binary::Requires symbols not provided by $provider"
    printf '%s' "$missing"
    return 1
  fi
}

audit_binary() {
  local binary="$1"
  local ldd_output

  echo "Auditing $binary"
  test -f "$binary"
  if ! ldd_output=$(ldd "$binary" 2>&1); then
    echo "$ldd_output"
    echo "::error file=$binary::ldd failed"
    return 1
  fi

  echo "$ldd_output"
  if echo "$ldd_output" | grep -q 'not found'; then
    echo "::error file=$binary::Unresolved shared library dependency"
    return 1
  fi

  fail_newer_glibc "$binary"
  fail_unavailable_symbols "$binary" libstdc++.so.6 'GLIBCXX_[0-9]+(\.[0-9]+)+|CXXABI_[0-9]+(\.[0-9]+)+'
  fail_unavailable_symbols "$binary" libasound.so.2 'ALSA_[0-9]+(\.[0-9]+)+([a-z0-9]+)?'
}

audit_binary "$STANDALONE"
audit_binary "$VST3"
