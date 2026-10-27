#!/usr/bin/env sh
# Build and run the host unit tests for core_logic + common/cocis_types.c.
# Uses the system gcc if present, otherwise a throwaway gcc Docker container.
set -e
cd "$(dirname "$0")/../.."          # -> firmware/

SRCS="test/host/test_core_logic.c components/core_logic/*.c components/common/cocis_types.c"
INCS="-Icomponents/core_logic/include -Icomponents/common/include"
BUILD="gcc -std=c11 -Wall -Wextra -Werror $INCS $SRCS -o /tmp/test_core_logic && /tmp/test_core_logic"

if command -v gcc >/dev/null 2>&1; then
    sh -c "$BUILD"
else
    MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W 2>/dev/null || pwd):/src" -w /src gcc:14 sh -c "$BUILD"
fi
