#!/bin/sh
### BEGIN INIT INFO
# Provides:          aesdsocket
# Required-Start:    $remote_fs $syslog
# Required-Stop:     $remote_fs $syslog
# Default-Start:     S
# Default-Stop:      0 6
# Short-Description: Start aesdsocket daemon
### END INIT INFO

PIDFILE=/var/run/aesdsocket.pid
DAEMON=/home/vasu/AELD/assignments-3-and-later-VasuPadsumbia/server/aesdsocket
DAEMON_ARGS="-d"

case "$1" in
  start)
    echo "Starting aesdsocket..."
    start-stop-daemon -S -b -m -p "$PIDFILE" --exec "$DAEMON" -- $DAEMON_ARGS
    ;;
  stop)
    echo "Stopping aesdsocket..."
    start-stop-daemon -K -p "$PIDFILE"
    ;;
  restart)
    $0 stop
    $0 start
    ;;
  *)
    echo "Usage: $0 {start|stop|restart}"
    exit 1
    ;;
esac

exit 0
