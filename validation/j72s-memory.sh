#!/system/bin/sh
# Sample Android memory without starting services or changing device state.
set -eu
export LC_ALL=C

samples=${1:-30}
case "$samples" in
    ''|*[!0-9]*)
        printf 'Usage: sh %s [samples from 1 to 600]\n' "$0" >&2
        exit 2
        ;;
esac
if [ "${#samples}" -gt 3 ] || [ "$samples" -lt 1 ] || [ "$samples" -gt 600 ]; then
    printf 'Sample count must be between 1 and 600\n' >&2
    exit 2
fi

low_ram=$(getprop ro.config.low_ram 2>/dev/null || true)
boot_completed=$(getprop sys.boot_completed 2>/dev/null || true)
printf '# ro.config.low_ram=%s sys.boot_completed=%s\n' "${low_ram:-unknown}" "${boot_completed:-unknown}"
printf 'sample\tmem_total_kib\tmem_available_kib\tswap_used_kib\tzram_physical_bytes\tmemory_full_avg10_percent\n'

sample=1
while [ "$sample" -le "$samples" ]; do
    memory=$(awk '
        $1 == "MemTotal:" { total = $2 }
        $1 == "MemAvailable:" { available = $2 }
        $1 == "SwapTotal:" { swap_total = $2 }
        $1 == "SwapFree:" { swap_free = $2 }
        END {
            if (!total || available == "" || swap_total == "" || swap_free == "")
                exit 1
            printf "%d\t%d\t%d", total, available, swap_total - swap_free
        }
    ' /proc/meminfo)
    zram=$(awk 'NR == 1 { print $3 }' /sys/block/zram0/mm_stat 2>/dev/null || true)
    pressure=$(awk '$1 == "full" { sub(/^avg10=/, "", $2); print $2 }' \
        /proc/pressure/memory 2>/dev/null || true)
    printf '%s\t%s\t%s\t%s\n' "$sample" "$memory" "${zram:-NA}" "${pressure:-NA}"
    sample=$((sample + 1))
    if [ "$sample" -le "$samples" ]; then
        sleep 1
    fi
done
