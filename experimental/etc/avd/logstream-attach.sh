#!/bin/bash

PORT=0
SOCAT_CONSOLE="file:$(tty),raw,echo=0"

while (( $# > 0 )); do
  case $1 in
    -crlf)
      SOCAT_CONSOLE="${SOCAT_CONSOLE},crlf"
      ;;
    *)
      PORT=$1
  esac
  shift
done


if (( $PORT == 0 )); then
  echo Need port number.
  read
  exit 1
fi

while ! nc -q 0 localhost ${PORT} </dev/null; do
  echo -n '.'
  usleep 100000
done
echo

socat ${SOCAT_CONSOLE} tcp:localhost:${PORT}
if (( $? == 127 )); then
  echo "Install socat first:"
  echo "sudo apt-get install socat"
  read
fi

