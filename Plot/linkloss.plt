set xrange [0:120]
set yrange [0:1]
set key right
set grid

set xlabel 'time'
set ylabel 'loss'

plot "./Data/link45.loss" using 1:2 \
     with linespoints pt 7 ps 0.9 t "Sender-1"
pause -1