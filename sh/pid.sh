export PID=$$
export SCRIPT_NAME=$(realpath $(basename $0))

RED='\033[0;31m'
END='\033[0m'

lock="/tmp/${SCRIPT_NAME//\//_}.pid"
mkdir -p $(dirname $lock)

if [[ -f "$lock" ]]; then
  pid=$(cat $lock)
  while kill $pid >/dev/null 2>&1; do
    echo "$SCRIPT_NAME: pid $pid running , trying kill it"
    sleep 1
    echo -e "$RED$ kill -9 $pid$END"
    err=$(kill -9 $pid 2>&1 >/dev/null) || true
    if [[ -n "$err" && "$err" != *"No such process"* ]]; then
      echo "$err" >&2
    fi
  done
fi

trap "rm -f $lock; exit $?" INT TERM EXIT
echo $PID >$lock
