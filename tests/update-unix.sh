#!/bin/sh
# Runs local shell fixtures only; keeps test files under build/.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$repo/build"
root=$(mktemp -d "$repo/build/updater-unix-tests.XXXXXX")
for scenario in success restart-failure invalid-path; do
    case_dir="$root/$scenario with spaces"
    stage="$case_dir/.iv-update-test"
    mkdir -p "$stage"
    target="$case_dir/viewer"
    source="$stage/download"
    ready="$stage/ready"
    commit="$stage/commit"
    log="$stage/update.log"
    cat >"$target" <<'FIXTURE'
#!/bin/sh
printf '%s' "${1-}" >"$(dirname "$0")/started.txt"
sleep 6
FIXTURE
    cp "$target" "$case_dir/original"
    cp "$target" "$source"
    printf '\n# New version\n' >>"$source"
    if [ "$scenario" = restart-failure ]; then printf '#!/bin/sh\nexit 1\n' >"$source"; fi
    chmod +x "$target" "$source"
    cp "$source" "$case_dir/expected"
    sleep 2 &
    old_pid=$!
    image="/test images/photo's \$1.png"
    if [ "$scenario" = invalid-path ]; then source="$case_dir/original"; fi
    sh "$repo/packaging/update-unix.sh" "$target" "$source" "$ready" "$log" "$old_pid" "$image" "$commit" &
    helper_pid=$!
    if [ "$scenario" = invalid-path ]; then
        if wait "$helper_pid"; then echo 'Invalid path accepted'; exit 1; fi
        cmp "$target" "$case_dir/original"
        echo 'PASS: invalid staging path leaves target unchanged'
        continue
    fi
    attempt=0
    while [ ! -f "$ready" ]; do
        attempt=$((attempt + 1))
        if [ "$attempt" -ge 10 ]; then echo 'Helper did not become ready'; exit 1; fi
        sleep 1
    done
    cmp "$target" "$case_dir/original"
    printf commit >"$commit"
    if [ "$scenario" = restart-failure ]; then
        if wait "$helper_pid"; then echo 'Expected update failure'; exit 1; fi
        cmp "$target" "$case_dir/expected"
        echo 'PASS: failed restart is reported without rollback'
        test ! -e "$stage/previous"
        continue
    else
        wait "$helper_pid"
        cmp "$target" "$case_dir/expected"
        test ! -e "$stage/previous"
        echo 'PASS: replacement without backup'
    fi
    test "$(cat "$case_dir/started.txt")" = "$image"
    echo 'PASS: restart preserves image argument'
done
