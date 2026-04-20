#!/bin/sh
set -eu

if [ "$#" -lt 4 ]; then
    echo "uso: $0 <binario> <vm_count> <scenario_name> <success_pattern> [expected_send] [expected_receive]" >&2
    exit 2
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)

BINARY_REL=$1
VM_COUNT=$2
SCENARIO_NAME=$3
SUCCESS_PATTERN=$4
EXPECTED_SEND=${5:-}
EXPECTED_RECEIVE=${6:-}
TIMEOUT_SEC=${TIMEOUT_SEC:-180}
QEMU_CPU=${QEMU_CPU:-default}
QEMU_BIN=${QEMU_BIN:-qemu-system-x86_64}
QEMU_MACHINE=${QEMU_MACHINE:-}
QEMU_NET_DEV=${QEMU_NET_DEV:-}
LOGS_DIR=${LOGS_DIR:-$REPO_ROOT/logs}
KEEP_ARTIFACTS=${KEEP_ARTIFACTS:-1}
ARTIFACTS_FILE=${ARTIFACTS_FILE:-}
SUCCESS_GRACE_SEC=0

case "$SCENARIO_NAME" in
    stress)
        SUCCESS_GRACE_SEC=3
        ;;
esac

if [ -t 1 ]; then
    COLOR_RED=$(printf '\033[31m')
    COLOR_GREEN=$(printf '\033[32m')
    COLOR_YELLOW=$(printf '\033[33m')
    COLOR_BLUE=$(printf '\033[34m')
    COLOR_RESET=$(printf '\033[0m')
else
    COLOR_RED=''
    COLOR_GREEN=''
    COLOR_YELLOW=''
    COLOR_BLUE=''
    COLOR_RESET=''
fi

info() {
    printf '%s%s%s\n' "$COLOR_BLUE" "$1" "$COLOR_RESET"
}

success() {
    printf '%s%s%s\n' "$COLOR_GREEN" "$1" "$COLOR_RESET"
}

warn() {
    printf '%s%s%s\n' "$COLOR_YELLOW" "$1" "$COLOR_RESET"
}

error() {
    printf '%s%s%s\n' "$COLOR_RED" "$1" "$COLOR_RESET" >&2
}

KERNEL="$REPO_ROOT/kernel/Image"
BASE_INITRAMFS="$REPO_ROOT/kernel/initramfs.cpio"
BINARY_PATH="$REPO_ROOT/$BINARY_REL"
mkdir -p "$LOGS_DIR" "$LOGS_DIR/$SCENARIO_NAME"
RUN_STAMP=$(date +%Y%m%d-%H%M%S)
TMP_ROOT=$(mktemp -d "$LOGS_DIR/$SCENARIO_NAME/${RUN_STAMP}.XXXXXX")
SCENARIO_LATEST_LINK="$LOGS_DIR/$SCENARIO_NAME/latest"
GLOBAL_LATEST_LINK="$LOGS_DIR/latest"
INITRAMFS_PATH="$TMP_ROOT/test.cpio"
LOG_DIR="$TMP_ROOT/logs"
MCAST_PORT=$(expr 12000 + $$ % 1000)
PIDS=""

ln -sfn "$TMP_ROOT" "$SCENARIO_LATEST_LINK"
ln -sfn "$TMP_ROOT" "$GLOBAL_LATEST_LINK"

if [ -n "$ARTIFACTS_FILE" ]; then
    printf '%s\n' "$TMP_ROOT" > "$ARTIFACTS_FILE"
fi

cleanup() {
    status=$?
    for pid in $PIDS; do
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
    if [ $status -ne 0 ]; then
        error "[test:$SCENARIO_NAME] logs preservados em $LOG_DIR"
        error "[test:$SCENARIO_NAME] initramfs preservado em $INITRAMFS_PATH"
        exit $status
    fi
    if [ "$KEEP_ARTIFACTS" = "1" ]; then
        success "[test:$SCENARIO_NAME] artefatos preservados em $TMP_ROOT"
        exit 0
    fi
    rm -f "$SCENARIO_LATEST_LINK" "$GLOBAL_LATEST_LINK"
    rm -rf "$TMP_ROOT"
}

trap cleanup EXIT INT TERM

log_matches_expectations() {
    logfile=$1

    if [ ! -f "$logfile" ]; then
        return 1
    fi

    if ! grep -aFq "$SUCCESS_PATTERN" "$logfile"; then
        return 1
    fi

    if [ -n "$EXPECTED_SEND" ]; then
        send_count=$(grep -ac "enviou:" "$logfile" || true)
        if [ "$send_count" != "$EXPECTED_SEND" ]; then
            return 1
        fi
    fi

    if [ -n "$EXPECTED_RECEIVE" ]; then
        receive_count=$(grep -ac "recebeu:" "$logfile" || true)
        if [ "$receive_count" != "$EXPECTED_RECEIVE" ]; then
            return 1
        fi
    fi

    return 0
}

success_highlight() {
    logfile=$1

    if [ ! -f "$logfile" ]; then
        return 1
    fi

    grep -aF "$SUCCESS_PATTERN" "$logfile" | tail -n 1
}

last_matching_line() {
    logfile=$1
    pattern=$2

    if [ ! -f "$logfile" ]; then
        return 1
    fi

    grep -aF "$pattern" "$logfile" | tail -n 1
}

extract_kv_field() {
    line=$1
    key=$2

    printf '%s\n' "$line" | sed -n "s/.*[[:space:]]$key=\\([^[:space:]]*\\).*/\\1/p"
}

print_execution_summary() {
    scenario_name=$1
    vm_label=$2
    logfile=$3

    case "$scenario_name" in
        stress)
            sender_line=$(last_matching_line "$logfile" "SEND_DONE" || true)
            listener_line=$(last_matching_line "$logfile" "RESUMO" || true)

            if [ -n "$sender_line" ]; then
                send_drops=$(extract_kv_field "$sender_line" "local_send_drops")
                done_drops=$(extract_kv_field "$sender_line" "local_done_drops")

                if [ -n "$send_drops" ] || [ -n "$done_drops" ]; then
                    info "[test:$scenario_name] $vm_label envio: local_send_drops=${send_drops:-?} local_done_drops=${done_drops:-?}"
                else
                    info "[test:$scenario_name] $vm_label dados de envio: $sender_line"
                fi
            fi
            if [ -n "$listener_line" ]; then
                intra_lost=$(extract_kv_field "$listener_line" "intra_lost")
                intra_loss_pct=$(extract_kv_field "$listener_line" "intra_loss_pct")
                inter_lost=$(extract_kv_field "$listener_line" "inter_lost")
                inter_loss_pct=$(extract_kv_field "$listener_line" "inter_loss_pct")
                dupes_inter=$(extract_kv_field "$listener_line" "dupes_inter")

                if [ -n "$intra_lost" ] || [ -n "$inter_lost" ] || [ -n "$dupes_inter" ]; then
                    if [ "${intra_lost:-0}" = "0" ] && [ "${inter_lost:-0}" = "0" ] && [ "${dupes_inter:-0}" = "0" ]; then
                        success "[test:$scenario_name] $vm_label recepcao: intra_lost=${intra_lost:-0} (${intra_loss_pct:-0}%) inter_lost=${inter_lost:-0} (${inter_loss_pct:-0}%) dupes_inter=${dupes_inter:-0}"
                    else
                        warn "[test:$scenario_name] $vm_label recepcao: intra_lost=${intra_lost:-?} (${intra_loss_pct:-?}%) inter_lost=${inter_lost:-?} (${inter_loss_pct:-?}%) dupes_inter=${dupes_inter:-?}"
                    fi
                else
                    info "[test:$scenario_name] $vm_label dados de recepcao: $listener_line"
                fi
            fi
            ;;
    esac
}

if [ ! -x "$BINARY_PATH" ]; then
    error "[test:$SCENARIO_NAME] binario ausente ou sem permissao: $BINARY_PATH"
    exit 1
fi

mkdir -p "$LOG_DIR"

info "[test:$SCENARIO_NAME] preparando initramfs temporario"
info "[test:$SCENARIO_NAME] logs em $LOG_DIR"

WORKDIR=$(mktemp -d "/tmp/${SCENARIO_NAME}-rootfs.XXXXXX")
(
    cd "$WORKDIR"
    cpio -id < "$BASE_INITRAMFS" >/dev/null
    cp "$BINARY_PATH" ./main
    chmod +x ./main
    cat > init <<'EOF'
#!/bin/sh
echo "[init] start"
mkdir -p /proc /sys /dev
mount -t proc proc /proc >/dev/null 2>&1 || echo "[init] aviso: falha ao montar /proc"
mount -t sysfs none /sys >/dev/null 2>&1 || echo "[init] aviso: falha ao montar /sys"
mount -t devtmpfs none /dev >/dev/null 2>&1 || echo "[init] aviso: falha ao montar /dev"
ip link set dev eth0 up >/dev/null 2>&1 || echo "[init] aviso: falha ao subir eth0"
echo "[init] executando /main"
/main
status=$?
echo "[init] /main saiu com status ${status}"
echo "[init] aguardando encerramento pelo host"
while :; do sleep 3600; done
EOF
    chmod +x ./init
    find . | cpio -o -H newc > "$INITRAMFS_PATH" 2>/dev/null
)
rm -rf "$WORKDIR"

info "[test:$SCENARIO_NAME] subindo $VM_COUNT VM(s) com cpu=$QEMU_CPU"

case "$(basename "$QEMU_BIN")" in
    qemu-system-x86_64|qemu-system-i386)
        : "${QEMU_MACHINE:=}"
        : "${QEMU_NET_DEV:=virtio-net-pci}"
        ;;
    qemu-system-riscv64|qemu-system-riscv32|qemu-system-aarch64|qemu-system-arm)
        : "${QEMU_MACHINE:=virt}"
        : "${QEMU_NET_DEV:=virtio-net-device}"
        ;;
    *)
        : "${QEMU_MACHINE:=}"
        : "${QEMU_NET_DEV:=virtio-net-pci}"
        ;;
esac

CPU_ARGS=""
if [ "$QEMU_CPU" != "default" ] && [ -n "$QEMU_CPU" ]; then
    CPU_ARGS="-cpu $QEMU_CPU"
fi

MACHINE_ARGS=""
if [ -n "$QEMU_MACHINE" ]; then
    MACHINE_ARGS="-machine $QEMU_MACHINE"
fi

vm_index=1
while [ "$vm_index" -le "$VM_COUNT" ]; do
    mac_suffix=$(printf "%02x" "$vm_index")
    "$QEMU_BIN" \
        $MACHINE_ARGS \
        -nographic \
        -m 512 \
        -kernel "$KERNEL" \
        -initrd "$INITRAMFS_PATH" \
        -append "root=/dev/ram rw console=ttyS0 so2.vm_id=$vm_index" \
        -netdev socket,id=vlan0,mcast=230.0.0.1:"$MCAST_PORT" \
        -device "$QEMU_NET_DEV",netdev=vlan0,mac=52:54:00:12:34:"$mac_suffix" \
        -serial file:"$LOG_DIR/vm${vm_index}.log" \
        -monitor none \
        -no-reboot \
        $CPU_ARGS &
    PIDS="$PIDS $!"
    vm_index=$(expr "$vm_index" + 1)
done

info "[test:$SCENARIO_NAME] aguardando encerramento das VM(s) (timeout ${TIMEOUT_SEC}s)"

start_ts=$(date +%s)
last_reported=0
completed=0
while :; do
    running=0
    for pid in $PIDS; do
        if kill -0 "$pid" 2>/dev/null; then
            running=1
            break
        fi
    done

    success_count=0
    vm_index=1
    while [ "$vm_index" -le "$VM_COUNT" ]; do
        logfile="$LOG_DIR/vm${vm_index}.log"
        if log_matches_expectations "$logfile"; then
            success_count=$(expr "$success_count" + 1)
        fi
        vm_index=$(expr "$vm_index" + 1)
    done

    if [ "$success_count" -eq "$VM_COUNT" ]; then
        completed=1
        success "[test:$SCENARIO_NAME] criterios de sucesso observados nos logs"
        if [ "$SUCCESS_GRACE_SEC" -gt 0 ]; then
            info "[test:$SCENARIO_NAME] aguardando ${SUCCESS_GRACE_SEC}s para consolidar os dados finais"
            sleep "$SUCCESS_GRACE_SEC"
        fi
        success "[test:$SCENARIO_NAME] encerrando VM(s)"
        break
    fi

    if [ "$running" -eq 0 ]; then
        break
    fi

    now_ts=$(date +%s)
    elapsed=$((now_ts - start_ts))
    if [ "$elapsed" -ge "$TIMEOUT_SEC" ]; then
        error "[test:$SCENARIO_NAME] timeout aguardando as VM(s)"
        for logfile in "$LOG_DIR"/vm*.log; do
            if [ -f "$logfile" ]; then
                warn "[test:$SCENARIO_NAME] ultimas linhas de $logfile"
                tail -n 40 "$logfile" >&2 || true
            fi
        done
        exit 1
    fi

    if [ "$elapsed" -ge $((last_reported + 10)) ]; then
        warn "[test:$SCENARIO_NAME] ainda aguardando... ${elapsed}s"
        last_reported=$elapsed
    fi

    sleep 1
done

for pid in $PIDS; do
    if kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
    fi
done

for pid in $PIDS; do
    wait "$pid" 2>/dev/null || true
done

vm_index=1
while [ "$vm_index" -le "$VM_COUNT" ]; do
    logfile="$LOG_DIR/vm${vm_index}.log"
    if ! log_matches_expectations "$logfile"; then
        error "[test:$SCENARIO_NAME] padrao de sucesso ausente em $logfile"
        tail -n 40 "$logfile" >&2 || true
        exit 1
    fi

    highlight=$(success_highlight "$logfile" || true)
    if [ -n "$highlight" ]; then
        success "[test:$SCENARIO_NAME] vm${vm_index} ok: $highlight"
    else
        success "[test:$SCENARIO_NAME] vm${vm_index} ok"
    fi
    print_execution_summary "$SCENARIO_NAME" "vm${vm_index}" "$logfile"
    vm_index=$(expr "$vm_index" + 1)
done

if [ "$completed" -ne 1 ]; then
    error "[test:$SCENARIO_NAME] as VM(s) encerraram sem satisfazer todos os criterios de sucesso"
    exit 1
fi

success "[test:$SCENARIO_NAME] suite concluida"
