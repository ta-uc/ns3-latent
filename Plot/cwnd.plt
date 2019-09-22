set xrange [2:120]
set yrange [0:70000]
set key right
set grid

set xlabel 'time'
set ylabel 'cwnd'

plot "./Data/Sender 1.cwnd" using 1:2 \
     with linespoints pt 7 ps 0.9 t "Sender-1",\
     "./Data/Sender 2.cwnd" using 1:2 \
     with linespoints pt 9 ps 0.9 t "Sender-1",\
     "./Data/Sender 3.cwnd" using 1:2 \
     with linespoints pt 13 ps 0.9 t "Sender-1"
pause -1
