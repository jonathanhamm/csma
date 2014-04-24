  kill -9 $(pgrep -f 'client|csma') 2>/dev/null
  ipcrm -M 0xdeadbeac 2>/dev/null
  ipcrm -M 0xdeadbea5 2>/dev/null
echo clean
