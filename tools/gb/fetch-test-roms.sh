#!/bin/sh
# ROM di prova senza licenza esplicita: non vanno nel repository. Le scarica in firmware/test/roms/.fetched/
# con impronta fissa; test_gb_emu le usa se ci sono e le salta se mancano.
set -eu
DIR="$(cd "$(dirname "$0")/../.." && pwd)/firmware/test/roms/.fetched"
mkdir -p "$DIR"
URL=https://raw.githubusercontent.com/retrio/gb-test-roms/c240dd7d700e5c0b00a7bbba52b53e4ee67b5f15/cpu_instrs/cpu_instrs.gb
SHA=8c5e12f41e0ba5bbca796944f92ffe6de28809198682c4332e38d1b3cf56fcf2
curl -fsSL -o "$DIR/cpu_instrs.gb.part" "$URL"
echo "$SHA  $DIR/cpu_instrs.gb.part" | shasum -a 256 -c -
mv "$DIR/cpu_instrs.gb.part" "$DIR/cpu_instrs.gb"
echo "cpu_instrs.gb in $DIR"
