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
QEMU_CPU=${QEMU_CPU:-max}
KEEP_ARTIFACTS=${KEEP_ARTIFACTS:-0}
ARTIFACTS_FILE=${ARTIFACTS_FILE:-}

KERNEL="$REPO_ROOT/kernel/Image"
BASE_INITRAMFS="$REPO_ROOT/kernel/initramfs.cpio"
BINARY_PATH="$REPO_ROOT/$BINARY_REL"
TMP_ROOT=$(mktemp -d "/tmp/${SCENARIO_NAME}.XXXXXX")
INITRAMFS_PATH="$TMP_ROOT/test.cpio"
LOG_DIR="$TMP_ROOT/logs"
MCAST_PORT=$(expr 12000 + $$ % 1000)
PIDS=""

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
        echo "[test:$SCENARIO_NAME] logs preservados em $LOG_DIR" >&2
        echo "[test:$SCENARIO_NAME] initramfs preservado em $INITRAMFS_PATH" >&2
        exit $status
    fi
    if [ "$KEEP_ARTIFACTS" = "1" ]; then
        echo "[test:$SCENARIO_NAME] artefatos preservados em $TMP_ROOT"
        exit 0
    fi
    rm -rf "$TMP_ROOT"
}

trap cleanup EXIT INT TERM

log_matches_expectations() {
    logfile=$1

    if [ ! -f "$logfile" ]; then
        return 1
    fi

    if ! grep -Fq "$SUCCESS_PATTERN" "$logfile"; then
        return 1
    fi

    if [ -n "$EXPECTED_SEND" ]; then
        send_count=$(grep -c "enviou:" "$logfile" || true)
        if [ "$send_count" != "$EXPECTED_SEND" ]; then
            return 1
        fi
    fi

    if [ -n "$EXPECTED_RECEIVE" ]; then
        receive_count=$(grep -c "recebeu:" "$logfile" || true)
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

    grep -F "$SUCCESS_PATTERN" "$logfile" | tail -n 1
}

if [ ! -x "$BINARY_PATH" ]; then
    echo "[test:$SCENARIO_NAME] binario ausente ou sem permissao: $BINARY_PATH" >&2
    exit 1
fi

mkdir -p "$LOG_DIR"

echo "[test:$SCENARIO_NAME] preparando initramfs temporario"

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

echo "[test:$SCENARIO_NAME] subindo $VM_COUNT VM(s) com cpu=$QEMU_CPU"

vm_index=1
while [ "$vm_index" -le "$VM_COUNT" ]; do
    mac_suffix=$(printf "%02x" "$vm_index")
    qemu-system-riscv64 \
        -machine virt \
        -cpu "$QEMU_CPU" \
        -nographic \
        -m 512 \
        -kernel "$KERNEL" \
        -initrd "$INITRAMFS_PATH" \
        -append "root=/dev/ram rw console=ttyS0 so2.vm_id=$vm_index" \
        -netdev socket,id=vlan0,mcast=230.0.0.1:"$MCAST_PORT" \
        -device virtio-net-device,netdev=vlan0,mac=52:54:00:12:34:"$mac_suffix" \
        -serial file:"$LOG_DIR/vm${vm_index}.log" \
        -monitor none \
        -no-reboot &
    PIDS="$PIDS $!"
    vm_index=$(expr "$vm_index" + 1)
done

echo "[test:$SCENARIO_NAME] aguardando encerramento das VM(s) (timeout ${TIMEOUT_SEC}s)"

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
        echo "[test:$SCENARIO_NAME] criterios de sucesso observados nos logs; encerrando VM(s)"
        break
    fi

    if [ "$running" -eq 0 ]; then
        break
    fi

    now_ts=$(date +%s)
    elapsed=$((now_ts - start_ts))
    if [ "$elapsed" -ge "$TIMEOUT_SEC" ]; then
        echo "[test:$SCENARIO_NAME] timeout aguardando as VM(s)" >&2
        for logfile in "$LOG_DIR"/vm*.log; do
            if [ -f "$logfile" ]; then
                echo "[test:$SCENARIO_NAME] ultimas linhas de $logfile" >&2
                tail -n 40 "$logfile" >&2 || true
            fi
        done
        exit 1
    fi

    if [ "$elapsed" -ge $((last_reported + 10)) ]; then
        echo "[test:$SCENARIO_NAME] ainda aguardando... ${elapsed}s"
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
        echo "[test:$SCENARIO_NAME] padrao de sucesso ausente em $logfile" >&2
        tail -n 40 "$logfile" >&2 || true
        exit 1
    fi

    highlight=$(success_highlight "$logfile" || true)
    if [ -n "$highlight" ]; then
        echo "[test:$SCENARIO_NAME] vm${vm_index} ok: $highlight"
    else
        echo "[test:$SCENARIO_NAME] vm${vm_index} ok"
    fi
    vm_index=$(expr "$vm_index" + 1)
done

if [ "$completed" -ne 1 ]; then
    echo "[test:$SCENARIO_NAME] as VM(s) encerraram sem satisfazer todos os criterios de sucesso" >&2
    exit 1
fi

echo "[test:$SCENARIO_NAME] suite concluida"
