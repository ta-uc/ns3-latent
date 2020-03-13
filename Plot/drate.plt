set xrange [0:40]
#set yrange [1000000:2000000]
#set yrange [50000:8000000]
set key right
set grid
set terminal png
set output './drate.png'

set xlabel 'time'
set ylabel 'datarate'

plot "./Data/n0-n10.drate" with linespoints pt 1 ps 0.9 t "n0-n10",\
 "./Data/n2-n3.drate" with linespoints pt 1 ps 0.9 t "n2-n3"
