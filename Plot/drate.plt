set xrange [0:15]
#set yrange [1000000:2000000]
#set yrange [50000:8000000]
set key right
set grid
set terminal png
set output './drate.png'

set xlabel 'time'
set ylabel 'datarate'

plot "./Data/n9-n2.drate" using 1:2 \
     with linespoints pt 1 ps 0.9 t "n9-n2"
pause -1
