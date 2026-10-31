#!/usr/bin/env bash
# Stage melonds-pgo: -fprofile-use rebuild of the melonDS DS core. Needs MELONDS_PROFILE=<dir|tgz> from the device.
# See stages/melonds.sh and build/README.md "melonDS PGO".
export MELONDS_MODE=use
exec "$(dirname "$0")/melonds.sh" "$@"
