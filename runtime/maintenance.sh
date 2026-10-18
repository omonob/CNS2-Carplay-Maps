#!/proc/boot/ksh
# Stop ONLY project helpers, never SSH/network/watchdog/native services.
set -eu
PATH=/proc/boot:/extbin/apps/bin:/extbin/apps/usr/bin:/ffs/sbin
export PATH
umask 077
ACTION=$1; TOKEN=$2; SERIAL=$3; TOOL=$4
case "$TOKEN" in ''|*[!a-f0-9]*) exit 130;; esac
[ "${#TOKEN}" -eq 16 ] || exit 130
[ -x "$TOOL" ] && [ ! -L "$TOOL" ] || exit 130
[ "$("$TOOL" --identity)" = "SERIAL=$SERIAL" ] || exit 131
P=/persistence/.v43c
S=/ramdisk/.cns2-v43-maint
O=/ramdisk/.cns2-v43
RUN=/ramdisk/cns275-carplayvc
[ ! -L "$P" ] && [ ! -L "$S" ] || exit 132
mkdir -p "$S" && chmod 700 "$S"
[ ! -L "$S/.operation.lock" ] || exit 133
# This unit has no rmdir. Use the same verified noclobber file primitive as
# prepare.sh/bootstrap.sh rather than a directory lock needing that command.
LOCK_TOKEN=$TOKEN
if ! (set -C; echo "$$ $LOCK_TOKEN $SERIAL" > "$S/.operation.lock") 2>/dev/null; then exit 133; fi
trap 'RC=$?; if [ -f "$S/.operation.lock" ] && [ "$(cat "$S/.operation.lock")" = "$$ $LOCK_TOKEN $SERIAL" ]; then rm -f "$S/.operation.lock"; fi; exit "$RC"' 0
trap 'exit 134' 1 2 15
snapshot()
{
    pidin ar > "$S/processes.new" || exit 135
    grep '^[ ]*[0-9][0-9]* ' "$S/processes.new" >/dev/null || exit 135
    mv "$S/processes.new" "$S/processes"
}
phone_check()
{
    if awk '$2 == "mm-ipod" || $2 == "/extbin/apps/usr/bin/mm-ipod" {f=1} END {exit f ? 0 : 1}' "$S/processes"; then
        echo V432_PHONE_CONNECTED; exit 136
    fi
}
owned_list()
{
    # Require exact executable/interpreter path; never trust PID files alone.
    awk -v o="$O" '
      $1 ~ /^[0-9]+$/ {
        owned=($2 == o "/cpvc-rgd-sync-cns2-v531.bin" ||
               $2 == o "/cpvc-cover-tap-cns2" ||
               $2 == o "/cpmap-bootstrap.sh" ||
               ($2 == "/proc/boot/ksh" && $3 == o "/cpmap-bootstrap.sh"))
        suspect=($0 ~ /cpmap-bootstrap[.]sh|cpvc-rgd-sync-cns2-v531[.]bin|cpvc-cover-tap-cns2/)
        if(suspect && !owned && $2 != "awk") bad=1
        if(owned) print $1
      }
      END {exit bad ? 1 : 0}' "$S/processes" > "$S/owned.pids" || {
        echo V432_UNKNOWN_PROJECT_PROCESS; exit 137;
    }
}
pidfile_check()
{
    for FILE in "$RUN/v43-bootstrap.lock" "$RUN/cpmap-v2-sync.pid" "$RUN/cpmap-v2-cover.pid"; do
        [ ! -L "$FILE" ] || exit 138
        [ ! -f "$FILE" ] || {
            PID=; read PID < "$FILE"
            case "$PID" in ''|*[!0-9]*) exit 138;; esac
            if awk -v p="$PID" '$1==p {f=1} END {exit f ? 0 : 1}' "$S/processes"; then
                grep "^$PID$" "$S/owned.pids" >/dev/null || {
                    echo V432_PIDFILE_NOT_OWNED; exit 138;
                }
            fi
        }
    done
}
protected_save()
{
    # Exclude transient sshd -R children; verify listener and supervisor.
    awk '$1 ~ /^[0-9]+$/ && ($2 ~ /\/sshd$/ || $3 ~ /remote_access_supervisor_v24[.]sh$/) && $0 !~ / -R([ ]|$)/ {print}' "$S/processes" > "$S/network.before"
    grep '/sshd ' "$S/network.before" >/dev/null || exit 139
}
protected_check()
{
    while IFS= read LINE; do
        [ -z "$LINE" ] || grep -F -x "$LINE" "$S/processes" >/dev/null || {
            echo V432_NETWORK_IDENTITY_CHANGED; exit 139;
        }
    done < "$S/network.before"
}
archive_completed_lock()
{
    [ -e "$P/install.lock" ] || return 0
    [ -f "$P/install.lock" ] && [ ! -L "$P/install.lock" ] || exit 141
    LP=; LG=; LS=
    read LP LG LS < "$P/install.lock"
    if [ -z "$LG" ]; then
        LG=$LP
    else
        case "$LP" in ''|*[!0-9]*) exit 141;; esac
        [ "$LS" = "$SERIAL" ] || exit 141
        if awk -v p="$LP" '$1==p {f=1} END {exit f ? 0 : 1}' "$S/processes"; then exit 141; fi
    fi
    [ "$LG" = "$OLD" ] || exit 141
    # The old generation must already be cold-loaded, and its published PM
    # byte-identical to the completed candidate with same-unit backup present.
    [ -f "$P/$OLD/pm.cfg.new" ] && [ ! -L "$P/$OLD/pm.cfg.new" ] &&
        [ -f /persistence/.v43b/$OLD/pm.active-before.cfg ] &&
        [ ! -L /persistence/pm.cfg ] || exit 141
    if awk '$1 ~ /^[0-9]+$/ && $0 ~ /\/commit[.]sh|\/rollback[.]sh/ {f=1} END {exit f ? 0 : 1}' "$S/processes"; then exit 141; fi
    D1=$("$TOOL" --digest /persistence/pm.cfg)
    D2=$("$TOOL" --digest "$P/$OLD/pm.cfg.new")
    [ -n "$D1" ] && [ "$D1" = "$D2" ] || exit 141
    ARCHIVE=$P/$OLD/completed-install-lock.$TOKEN
    [ ! -e "$ARCHIVE" ] && [ ! -L "$ARCHIVE" ] || exit 141
    mv "$P/install.lock" "$ARCHIVE"
    echo "V432_COMPLETED_LOCK_ARCHIVED old=$OLD; same-unit cold-loaded PM verified"
}
read_record()
{
    [ -f "$P/MAINTENANCE" ] && [ ! -L "$P/MAINTENANCE" ] || exit 140
    read REC_TOKEN OLD REC_SERIAL < "$P/MAINTENANCE"
    [ "$REC_TOKEN" = "$TOKEN" ] && [ "$REC_SERIAL" = "$SERIAL" ] || exit 140
    case "$OLD" in ''|*[!a-f0-9]*) exit 140;; esac
    [ "${#OLD}" -eq 16 ] || exit 140
    [ ! -L "$P/$OLD/serial" ] && [ "$(cat "$P/$OLD/serial")" = "$SERIAL" ] || exit 140
    ACTIVE=; [ ! -f "$P/ACTIVE" ] || read ACTIVE < "$P/ACTIVE"
}
FACTORY_ARCHIVE=
factory_idle()
{
    [ ! -e "$P/install.lock" ] && [ ! -L "$P/install.lock" ] &&
        [ ! -e "$S/HOLD" ] && [ ! -L "$S/HOLD" ] &&
        [ ! -e /ramdisk/.cns2-v43-state/READY ] && [ ! -L /ramdisk/.cns2-v43-state/READY ] &&
        [ ! -e /ramdisk/.cns2-v43-state/FAILED ] && [ ! -L /ramdisk/.cns2-v43-state/FAILED ] || {
        echo V437_FACTORY_ACTIVE_OR_UNKNOWN_STATE_STOP;exit 146;
    }
    snapshot;phone_check;owned_list;pidfile_check
    [ ! -s "$S/owned.pids" ] || { echo V437_FACTORY_PROJECT_RUNNING_STOP;exit 146; }
    # Exact argument ownership, not a broad name-based process kill. No signals.
    awk '
      $1~/^[0-9]+$/ {
        if($2~/(^|\/)procman$/){n++;if(NF!=4||$3!="-f"||$4!="/etc/procman_core.cfg")bad=1}
        if($2~/(^|\/)(commit|rollback|prepare|entry)[.]sh$/ ||
           $3~/(^|\/)(commit|rollback|prepare|entry)[.]sh$/ ||
           $0~/-Dcns2[.]encrypted[.]candidate=serial_v2/)bad=1
      }
      END {exit n==1&&!bad?0:1}' "$S/processes" || {
        echo V437_FACTORY_NATIVE_OR_TRANSACTION_STATE_STOP;exit 146;
    }
}
if [ "$ACTION" = factory-recover ];then
    # Only the package-verified factory proof may select this path. Never relax
    # commit.sh's safety gate or infer a dead transaction from its PID alone.
    PROOF=$5
    [ -f "$PROOF" ] && [ ! -L "$PROOF" ] || exit 146
    "$TOOL" --check-native "$PROOF"
    factory_idle
    if [ ! -e "$P/MAINTENANCE" ] && [ ! -L "$P/MAINTENANCE" ] &&
        [ ! -e "$P/DISABLED" ] && [ ! -L "$P/DISABLED" ];then
        echo V437_FACTORY_MAINTENANCE_CLEAN;exit 0
    fi
    [ -f "$P/MAINTENANCE" ] && [ ! -L "$P/MAINTENANCE" ] &&
        [ -f "$P/DISABLED" ] && [ ! -L "$P/DISABLED" ] &&
        [ -f "$P/ACTIVE" ] && [ ! -L "$P/ACTIVE" ] || {
        echo V437_FACTORY_INCOMPLETE_RECORD_STOP;exit 147;
    }
    read REC_TOKEN OLD REC_SERIAL EXTRA < "$P/MAINTENANCE"
    case "$REC_TOKEN:$OLD" in *[!a-f0-9:]*) exit 147;;esac
    [ "${#REC_TOKEN}" -eq 16 ] && [ "${#OLD}" -eq 16 ] &&
        [ "$REC_TOKEN" != "$OLD" ] && [ "$REC_TOKEN" != "$TOKEN" ] && [ "$OLD" != "$TOKEN" ] &&
        [ "$REC_SERIAL" = "$SERIAL" ] && [ -z "$EXTRA" ] &&
        [ "$(cat "$P/MAINTENANCE")" = "$REC_TOKEN $OLD $SERIAL" ] &&
        [ "$(cat "$P/DISABLED")" = "V432_MAINTENANCE $REC_TOKEN" ] &&
        [ "$(cat "$P/ACTIVE")" = "$OLD" ] || { echo V437_FACTORY_RECORD_IDENTITY_STOP;exit 147; }
    [ -d "$P/$OLD" ] && [ ! -L "$P/$OLD" ] &&
        [ -f "$P/$OLD/serial" ] && [ ! -L "$P/$OLD/serial" ] &&
        [ "$(cat "$P/$OLD/serial")" = "$SERIAL" ] &&
        [ -f "$P/$OLD/COMMITTED" ] && [ ! -L "$P/$OLD/COMMITTED" ] &&
        [ "$(cat "$P/$OLD/COMMITTED")" = "$OLD $SERIAL" ] || {
        echo V437_FACTORY_OLD_RECEIPT_STOP;exit 147;
    }
    [ ! -L "$P/$REC_TOKEN" ] &&
        [ ! -e "$P/$REC_TOKEN/COMMITTED" ] && [ ! -L "$P/$REC_TOKEN/COMMITTED" ] &&
        [ ! -e "$P/$REC_TOKEN/COMMIT_DISPATCHED" ] && [ ! -L "$P/$REC_TOKEN/COMMIT_DISPATCHED" ] || {
        echo V437_FACTORY_UNRESOLVED_TRANSACTION_STOP;exit 147;
    }
    # Archive FIRST on the same persistent filesystem. Interrupted or partial
    # records remain a stop condition, not permission to blindly delete them.
    FACTORY_ARCHIVE=$P/$OLD/factory-maintenance-recovery.$TOKEN
    [ ! -e "$FACTORY_ARCHIVE" ] && [ ! -L "$FACTORY_ARCHIVE" ] || exit 147
    mkdir "$FACTORY_ARCHIVE";chmod 700 "$FACTORY_ARCHIVE"
    for F in MAINTENANCE DISABLED ACTIVE;do cp -p "$P/$F" "$FACTORY_ARCHIVE/$F";done
    cp -p "$P/$OLD/COMMITTED" "$FACTORY_ARCHIVE/old.COMMITTED"
    cp "$S/processes" "$FACTORY_ARCHIVE/processes.before"
    awk '$1~/^[0-9]+$/ && ($2~/(^|\/)(io-pkt-v4|io-pkt-v6-hc|sshd|Most|NavInterface|CarPlay|procman|j9|watchdog)$/ || $3~/remote_access_supervisor_v24[.]sh$/) && $0!~/ -R([ ]|$)/ {print}' "$S/processes" > "$FACTORY_ARCHIVE/protected.before"
    awk '$2~/(^|\/)sshd$/ {f=1} END {exit f?0:1}' "$FACTORY_ARCHIVE/protected.before" || exit 139
    sync
    "$TOOL" --check-native "$PROOF"
    factory_idle
    awk 'FILENAME==ARGV[1] {wanted[$0]=1;n++;next} ($0 in wanted) {seen[$0]=1} END {c=0;for(k in seen)c++;exit c==n?0:1}' "$FACTORY_ARCHIVE/protected.before" "$S/processes" || {
        echo V437_FACTORY_PROTECTED_PROCESS_CHANGED_STOP;exit 139;
    }
    [ "$(cat "$P/MAINTENANCE")" = "$(cat "$FACTORY_ARCHIVE/MAINTENANCE")" ] &&
        [ "$(cat "$P/DISABLED")" = "$(cat "$FACTORY_ARCHIVE/DISABLED")" ] &&
        [ "$(cat "$P/ACTIVE")" = "$OLD" ] || exit 147
    # Reuse the existing same-unit release operation under its original lock.
    # This route explicitly forbids hot-resuming old helpers/native services.
    TOKEN=$REC_TOKEN
    ACTION=release
fi
case "$ACTION" in
 enter)
    [ ! -e "$P/MAINTENANCE" ] && [ ! -e "$S/HOLD" ] &&
        [ ! -e "$P/DISABLED" ] || exit 141
    OLD=; read OLD < "$P/ACTIVE"
    case "$OLD" in ''|*[!a-f0-9]*) exit 142;; esac
    [ "${#OLD}" -eq 16 ] && [ "$OLD" != "$TOKEN" ] || exit 142
    [ ! -L "$P/ACTIVE" ] && [ ! -L "$P/$OLD/serial" ] || exit 142
    [ "$(cat "$P/$OLD/serial")" = "$SERIAL" ] || exit 142
    READY=; [ ! -L /ramdisk/.cns2-v43-state/READY ] || exit 142
    [ -f /ramdisk/.cns2-v43-state/READY ] && read READY < /ramdisk/.cns2-v43-state/READY || exit 142
    [ "$READY" = "$OLD" ] || { echo V432_COLD_RESTART_REQUIRED; exit 142; }
    snapshot; phone_check; owned_list; pidfile_check; protected_save
    archive_completed_lock
    # This project-only gate is honored by older V43/V43.1 too. A network
    # interruption leaves same-unit recovery evidence, not disabled SSH.
    (set -C; echo "$TOKEN $OLD $SERIAL" > "$P/MAINTENANCE") || exit 141
    (set -C; echo "$TOKEN $OLD $SERIAL" > "$S/HOLD") || exit 141
    (set -C; echo "V432_MAINTENANCE $TOKEN" > "$P/DISABLED") || exit 141
    sleep 3
    snapshot; phone_check; owned_list; pidfile_check; protected_check
    cp "$S/owned.pids" "$S/stopped.pids"
    while read PID; do
        snapshot; owned_list
        if grep "^$PID$" "$S/owned.pids" >/dev/null; then kill -TERM "$PID"; fi
    done < "$S/stopped.pids"
    N=0
    while [ "$N" -lt 5 ]; do
        sleep 1; snapshot; owned_list; protected_check
        [ -s "$S/owned.pids" ] || break
        N=$((N+1))
    done
    if [ -s "$S/owned.pids" ]; then
        cp "$S/owned.pids" "$S/remaining.pids"
        while read PID; do
            snapshot; owned_list
            if grep "^$PID$" "$S/owned.pids" >/dev/null; then kill -KILL "$PID"; fi
        done < "$S/remaining.pids"
    fi
    N=0
    while [ "$N" -lt 4 ]; do
        sleep 1; snapshot; owned_list; protected_check
        [ ! -s "$S/owned.pids" ] || { echo V432_PROJECT_RESPAWN; exit 143; }
        N=$((N+1))
    done
    for FILE in "$RUN/v43-bootstrap.lock" "$RUN/cpmap-v2-sync.pid" "$RUN/cpmap-v2-cover.pid"; do
        [ ! -L "$FILE" ] || exit 138
        [ ! -f "$FILE" ] || {
            PID=; read PID < "$FILE"
            if awk -v p="$PID" '$1==p {f=1} END {exit f ? 0 : 1}' "$S/processes"; then exit 138; fi
            rm -f "$FILE"
        }
    done
    echo "V432_MAINTENANCE_READY token=$TOKEN old=$OLD SSH_NETWORK_UNCHANGED"
    ;;
 release)
    read_record
    if [ "$ACTIVE" = "$TOKEN" ]; then
        echo V432_COMMITTED_KEEP_HOLD_RESTART_MANUALLY; exit 0
    fi
    [ "$ACTIVE" = "$OLD" ] || exit 144
    [ ! -L "$P/DISABLED" ] || exit 140
    if [ -e "$P/DISABLED" ]; then
        [ "$(cat "$P/DISABLED")" = "V432_MAINTENANCE $TOKEN" ] || exit 140
    fi
    snapshot; phone_check; owned_list; pidfile_check
    if [ -f "$RUN/v43-bootstrap.lock" ]; then
        PID=; read PID < "$RUN/v43-bootstrap.lock"
        if ! awk -v p="$PID" '$1==p {f=1} END {exit f ? 0 : 1}' "$S/processes"; then
            rm -f "$RUN/v43-bootstrap.lock"
        fi
    fi
    [ ! -e "$S/HOLD" ] || {
        [ ! -L "$S/HOLD" ] && [ "$(cat "$S/HOLD")" = "$TOKEN $OLD $SERIAL" ] || exit 140
        rm -f "$S/HOLD"
    }
    [ ! -e "$P/DISABLED" ] || rm -f "$P/DISABLED"
    # QNX mv delegates cross-filesystem moves to /bin/cp, absent on CNS2.
    # Copy with the actual PATH-resolved cp, verify, then remove only this
    # exact transaction record. Atomic publish remains within the RAM FS.
    cp "$P/MAINTENANCE" "$S/released-record.part"
    [ "$(cat "$S/released-record.part")" = "$TOKEN $OLD $SERIAL" ] || exit 140
    mv "$S/released-record.part" "$S/released-record"
    rm -f "$P/MAINTENANCE"
    sync
    READY=; [ ! -f /ramdisk/.cns2-v43-state/READY ] || read READY < /ramdisk/.cns2-v43-state/READY
    if [ -z "$FACTORY_ARCHIVE" ] && [ "$READY" = "$OLD" ] && [ -f "$O/cpmap-bootstrap.sh" ]; then
        if ! grep "cpmap-bootstrap.sh" "$S/processes" | grep -v ' awk ' >/dev/null; then
            [ ! -e "$RUN/v43-bootstrap.lock" ] || exit 138
            /proc/boot/ksh "$O/cpmap-bootstrap.sh" </dev/null >> "$S/resume.log" 2>&1 &
        fi
        echo V432_OLD_HELPERS_RELEASED_NO_NATIVE_RESTART
    else
        echo V432_HOLD_RELEASED_RESTART_MANUALLY
    fi
    ;;
 finish-rollback)
    read_record
    BACK=$5
    case "$BACK" in ''|*[!a-f0-9]*) exit 145;; esac
    [ "${#BACK}" -eq 16 ] || exit 145
    [ "$(cat "$P/$BACK/serial")" = "$SERIAL" ] || exit 145
    EXPECTED=
    if [ -f /persistence/.v43b/$BACK/ACTIVE.original ]; then
        [ ! -L /persistence/.v43b/$BACK/ACTIVE.original ] || exit 145
        read EXPECTED < /persistence/.v43b/$BACK/ACTIVE.original
    else
        [ -f /persistence/.v43b/$BACK/ACTIVE.absent ] || exit 145
    fi
    [ "$ACTIVE" = "$EXPECTED" ] || exit 145
    [ ! -L "$P/DISABLED" ] || exit 145
    if [ -e "$P/DISABLED" ]; then
        D=$(cat "$P/DISABLED")
        [ -z "$D" ] || [ "$D" = "V432_MAINTENANCE $TOKEN" ] || exit 145
        rm -f "$P/DISABLED"
    fi
    cp "$P/MAINTENANCE" "$S/rollback-record.part"
    [ "$(cat "$S/rollback-record.part")" = "$TOKEN $OLD $SERIAL" ] || exit 140
    mv "$S/rollback-record.part" "$S/rollback-record"
    rm -f "$P/MAINTENANCE"
    sync
    echo V432_ROLLBACK_HELD_RESTART_MANUALLY
    ;;
 status)
    [ ! -f "$P/MAINTENANCE" ] || cat "$P/MAINTENANCE"
    [ ! -f "$S/HOLD" ] || cat "$S/HOLD"
    echo V432_MAINTENANCE_STATUS_ONLY
    ;;
 *) exit 130;;
esac
if [ -n "$FACTORY_ARCHIVE" ];then
    snapshot
    awk 'FILENAME==ARGV[1] {wanted[$0]=1;n++;next} ($0 in wanted) {seen[$0]=1} END {c=0;for(k in seen)c++;exit c==n?0:1}' "$FACTORY_ARCHIVE/protected.before" "$S/processes" || {
        echo V437_FACTORY_PROTECTED_PROCESS_CHANGED_STOP;exit 139;
    }
    echo "$TOKEN $OLD $SERIAL" > "$FACTORY_ARCHIVE/RELEASED"
    sync
    echo "V437_FACTORY_MAINTENANCE_RECOVERED archive=$FACTORY_ARCHIVE NO_HOT_RESTART SSH_NETWORK_PRESERVED"
fi
