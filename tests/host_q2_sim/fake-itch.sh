#!/bin/sh
# Stand-in for q2-pico8-itch in the host simulation: the same command line
# and output format, no network. Carts it "downloads" are copies of
# tests/demo.p8 (CC0).
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "$1" in
list)
    sleep 1
    [ "$4" = 1 ] && echo "P 1" || echo "P 0"
    printf '101\tCeleste Classic\tMaddy Thorson\tFree\thttps://maddymakesgames.itch.io/celeste-classic\tcart\n'
    printf '102\tA Very Long PICO-8 Game Title That Needs Trimming\tSomeone With A Long Name\tFree / name your price\thttps://someone.itch.io/long\tweb\n'
    printf '103\tPaid Cart\tStudio\t$3.00\thttps://studio.itch.io/paid\tcart\n'
    ;;
download)
    sleep 1
    case "$2" in
    *celeste-classic) name="Celeste Classic [itch 101].p8" ;;
    *long) name="A Very Long PICO-8 Game Title [itch 102].p8" ;;
    *) echo "Open the itch.io page for this download"; exit 1 ;;
    esac
    cp "$here/../demo.p8" "$3/$name" && echo "Cart added to your library"
    ;;
qr)
    i=0
    while [ $i -lt 25 ]; do
        j=0; row=""
        while [ $j -lt 25 ]; do
            if [ $(( (i * 7 + j * 3) % 5 )) -lt 2 ] || { [ $i -lt 7 ] && [ $j -lt 7 ]; }; then row="${row}1"; else row="${row}0"; fi
            j=$((j + 1))
        done
        echo "$row"; i=$((i + 1))
    done
    ;;
*) echo "Open the itch.io page for this download"; exit 1 ;;
esac
