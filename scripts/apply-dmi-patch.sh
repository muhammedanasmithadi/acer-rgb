#!/bin/sh
# Apply the DMI bypass patch to tuxedo-drivers DKMS source
# Requires root privileges
# This modifies kernel module source — opt in consciously

set -e

if [ "$(id -u)" -ne 0 ]; then
  echo "This script must be run as root. Use: sudo $0"
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PATCH="$SCRIPT_DIR/patches/tuxedo_dmi_bypass.patch"

# Find DKMS source directory
DKMS_SRC="/usr/src/tuxedo-drivers-4.22.1"
if [ ! -d "$DKMS_SRC" ]; then
  # Try to find it dynamically
  DKMS_SRC=$(ls -d /usr/src/tuxedo-drivers-* 2>/dev/null | head -1)
fi

if [ -z "$DKMS_SRC" ] || [ ! -d "$DKMS_SRC" ]; then
  echo "Error: tuxedo-drivers DKMS source not found in /usr/src/"
  echo "Install tuxedo-drivers first: sudo dnf install tuxedo-drivers"
  exit 1
fi

echo "DKMS source: $DKMS_SRC"
echo "Patch: $PATCH"
echo ""

# NOTE: the TUXEDO RPM lays DKMS sources out flat (no src/ level).
# Verified layout: /usr/src/tuxedo-drivers-X/tuxedo_compatibility_check.c
TARGET="$DKMS_SRC/tuxedo_compatibility_check/tuxedo_compatibility_check.c"
if [ ! -f "$TARGET" ]; then
  echo "Error: target not found: $TARGET"
  echo "Check the DKMS source layout under $DKMS_SRC and update TARGET."
  exit 1
fi
if grep -q "DMI bypass" "$TARGET"; then
  echo "Patch appears to already be applied. Skipping."
  echo "  ($TARGET)"
  echo ""
  echo "To force reapply, revert the file first:"
  echo "  cp $TARGET.bak $TARGET   (if you kept a backup)"
  exit 0
fi

echo "This will modify a kernel module source file."
echo "The change makes tuxedo_is_compatible() always return true,"
echo "bypassing the DMI hardware check."
echo ""
echo "Are you sure? (y/N): "
read -r CONFIRM
if [ "$CONFIRM" != "y" ] && [ "$CONFIRM" != "Y" ]; then
  echo "Aborted."
  exit 1
fi

# Apply the patch. The .patch uses a/ b/ prefixes with a src/ level
# (repo layout), so strip 2 components to match the flat RPM layout.
echo "Applying patch..."
if patch -d "$DKMS_SRC" -p2 < "$PATCH"; then
  echo "Patch applied successfully."
else
  echo "Patch failed. Trying fallback (insert early return)..."
  # Fallback: insert "return true;" as the first statement of
  # tuxedo_is_compatible() via python3.
  if [ -f "$TARGET" ]; then
    python3 - "$TARGET" <<'EOF'
import sys
p = sys.argv[1]
s = open(p).read()
old = "bool tuxedo_is_compatible(void) {"
assert s.count(old) == 1, "pattern not found exactly once"
marker = "\n\treturn true; /* DMI bypass for non-TUXEDO hardware */"
if "DMI bypass" not in s:
    s = s.replace(old, old + marker, 1)
    open(p, "w").write(s)
    print("Fallback patch applied.")
else:
    print("Marker already present, nothing to do.")
EOF
  fi
fi

# Verify (marker comment, not just "return true;" — the original
# function legitimately contains "return true;" inside its if body)
if grep -q "DMI bypass" "$TARGET"; then
  echo "Verification: patch applied correctly."
else
  echo "Warning: patch verification failed. Check $TARGET manually."
  exit 1
fi

# Rebuild via DKMS
DRIVER_VERSION=$(basename "$DKMS_SRC" | sed 's/tuxedo-drivers-//')
echo ""
echo "Rebuilding via DKMS (tuxedo-drivers/$DRIVER_VERSION)..."
dkms remove "tuxedo-drivers/$DRIVER_VERSION" --all 2>/dev/null || true
dkms add "$DKMS_SRC"
dkms build "tuxedo-drivers/$DRIVER_VERSION"
dkms install "tuxedo-drivers/$DRIVER_VERSION"
depmod -a

echo ""
echo "DMI bypass patch applied and modules rebuilt."
echo "Load the modules:  sudo modprobe clevo_wmi"
echo "Verify:           lsmod | grep -E 'tuxedo|clevo'"
