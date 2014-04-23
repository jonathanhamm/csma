  kill -9 $(pgrep -f client) 2>/dev/null
  ipcrm -M 0xdeadbeac 2>/dev/null
  ipcrm -M 0xdeadbea5 2>/dev/null

