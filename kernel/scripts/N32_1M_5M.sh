#!/bin/bash

# 每路参数：iface tq prop-seg phase-seg1 phase-seg2 sjw dtq dprop-seg dphase-seg1 dphase-seg2 dsjw termination
configs=(
    "can0 100 1 6 2 2 25 1 4 2 2 120"
    "can1 100 1 6 2 2 25 1 4 2 2 120"
    "can2 100 1 6 2 2 25 1 4 2 2 120"
    "can3 100 1 6 2 2 25 1 4 2 2 120"
    "can4 100 1 6 2 2 25 1 4 2 2 120"
    "can5 100 1 6 2 2 25 1 4 2 2 120"
    "can6 100 1 6 2 2 25 1 4 2 2 120"
    "can7 100 1 6 2 2 25 1 4 2 2 120"
)

for config in "${configs[@]}"; do
    read -r iface tq prop_seg phase_seg1 phase_seg2 sjw \
        dtq dprop_seg dphase_seg1 dphase_seg2 dsjw termination <<<"${config}"

    echo "配置 ${iface} ..."
    sudo ip link set "${iface}" down

    # tq 单位 ns
    sudo ip link set "${iface}" type can \
        tq "${tq}" \
        prop-seg "${prop_seg}" \
        phase-seg1 "${phase_seg1}" \
        phase-seg2 "${phase_seg2}" \
        sjw "${sjw}" \
        dtq "${dtq}" \
        dprop-seg "${dprop_seg}" \
        dphase-seg1 "${dphase_seg1}" \
        dphase-seg2 "${dphase_seg2}" \
        dsjw "${dsjw}" \
        fd on \
        restart-ms 100

    sudo ip link set "${iface}" mtu 72
    sudo ip link set "${iface}" up
    # sudo ip link set "${iface}" type can termination "${termination}"
    sudo ifconfig "${iface}" txqueuelen 1000

    echo "${iface} 配置完成"
done

echo ""
echo "显示所有CAN接口状态："
for config in "${configs[@]}"; do
    read -r iface _ <<<"${config}"
    echo "=== ${iface} 状态 ==="
    ip -details -s link show "${iface}"
    echo ""
done
