set xrange [0:15]
#set yrange [0:0.3]
set key right
set grid

set terminal png
set output './pathloss.png'

set xlabel 'time'
set ylabel 'loss'

plot "./Data/n9-n2.loss" using 1:2 \
     with linespoints pt 1 ps 0.9 t "n0-n2"
pause -1
