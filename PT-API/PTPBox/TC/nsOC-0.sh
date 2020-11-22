#!/bin/bash

TC0_NIC_dev=eth20
TC1_NIC_dev=eth21
TC_ns=TC
TC0_IP_ADDR=192.168.3.105/24
TC1_IP_ADDR=192.168.1.104/24

ip netns add $TC_ns

ip link set dev $TC0_NIC_dev netns $TC_ns
ip link set dev $TC1_NIC_dev netns $TC_ns

ip netns exec $TC_ns ip link set lo up

ip netns exec $TC_ns ip addr add $TC0_IP_ADDR dev $TC0_NIC_dev 
ip netns exec $TC_ns ip link set $TC0_NIC_dev up
ip netns exec $TC_ns ip addr add $TC1_IP_ADDR dev $TC1_NIC_dev
ip netns exec $TC_ns ip link set $TC1_NIC_dev up

ip netns exec $TC_ns ptp4l -i $TC0_NIC_dev -f OC-0.cfg -s -m -4 | tee OC-0.log 
