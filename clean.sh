  kill -9 $(pgrep -f client) 2>/dev/null
  ipcrm -M 0xdeadbeaf 2>/dev/null

