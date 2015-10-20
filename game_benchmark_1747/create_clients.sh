#!/bin/bash
COUNTER=0
until [ $COUNTER -eq 1000 ]; do 
  echo "Spawning $COUNTER"
  let COUNTER+=1
  ./client 128.100.13.206:8081 > /dev/null &
  sleep 0.1
done
	
