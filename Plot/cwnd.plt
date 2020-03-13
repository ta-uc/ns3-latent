set xrange [0:40]
#set yrange [0:70000]
set key right
set grid
set terminal png
set output './cwnd.png'

set xlabel 'time'
set ylabel 'cwnd'

plot "./Data/n0-n10.cwnd" using 1:2 with linespoints pt 0.5 ps 0.9 t "n0-n10",\
 "./Data/n2-n3.cwnd" using 1:2 with linespoints pt 0.5 ps 0.9 t "n2-n3"

