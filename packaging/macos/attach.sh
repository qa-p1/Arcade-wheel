# Sourced by the DMG scripts. `hdiutil attach` can answer "Resource temporarily unavailable" (EAGAIN) while the disk
# image service still holds an image it has just created or verified; that error alone is tried again, any other
# fails at once.
attach_dmg() { # attach_dmg MOUNTPOINT DMG
    local try out
    for try in 1 2 3 4 5; do
        if out=$(hdiutil attach -readonly -nobrowse -mountpoint "$1" "$2" 2>&1); then
            printf '%s\n' "$out"
            return 0
        fi
        printf '%s\n' "$out" >&2
        [[ "$out" == *"Resource temporarily unavailable"* && $try -lt 5 ]] || return 1
        sleep $((try * 2))
    done
}
