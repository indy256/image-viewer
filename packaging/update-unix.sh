#!/bin/sh
# All paths arrive as separate arguments, never as evaluated shell text.
set -eu
target=$1 source=$2 ready=$3 log=$4 viewer_pid=$5 image=$6 commit=$7
stage=$(dirname "$source")
exec >>"$log" 2>&1
test "$(dirname "$stage")" = "$(dirname "$target")"
test "$(dirname "$ready")" = "$stage"
test "$(dirname "$commit")" = "$stage"
test -e "$target"
test -e "$source"
if [ -d "$target" ]; then
    # Only the specific app bundle supplied by the viewer may be removed.
    case "$target" in /*.app) ;; *) exit 1 ;; esac
    test ! -L "$target"
    test -x "$target/Contents/MacOS/iv"
    test -x "$source/Contents/MacOS/iv"
fi
restart() {
    if [ -d "$target" ]; then
        if [ -n "$image" ]; then /usr/bin/open -n "$target" --args "$image";
        else /usr/bin/open -n "$target"; fi
    else
        if [ -n "$image" ]; then "$target" "$image" &
        else "$target" & fi
        new_pid=$!
        sleep 2
        kill -0 "$new_pid"
    fi
}
printf ready >"$ready"
attempt=0
while [ ! -f "$commit" ]; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 30 ]; then echo 'Update handoff was canceled.'; exit 1; fi
    sleep 1
done
attempt=0
while kill -0 "$viewer_pid" 2>/dev/null; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 120 ]; then echo 'Timed out waiting for viewer exit.'; exit 1; fi
    sleep 1
done
if [ -d "$target" ]; then
    # Directory replacement cannot overwrite a nonempty macOS bundle.
    rm -rf -- "$target"
fi
if ! mv -f "$source" "$target"; then
    echo 'Could not install the update.'
    if [ -e "$target" ]; then restart; fi
    exit 1
fi
if ! restart; then
    echo 'The updated viewer did not start.'
    exit 1
fi
echo 'Update installed.'
