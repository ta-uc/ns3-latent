set xrange [2:120]
set yrange [0.7:1]
set key right
set grid

set xlabel 'time'
set ylabel 'util'

plot "./Data/link45.util" using 1:2 \
     with linespoints pt 7 ps 0.9 t "Sender-1"
pause -1
