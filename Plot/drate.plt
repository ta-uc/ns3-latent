set xrange [2:140]
#set yrange [1000000:2000000]
set yrange [50000:8000000]
set key right
set grid

set xlabel 'time'
set ylabel 'datarate'

plot "./Data/Sender 1.drate" using 1:2 \
     with linespoints pt 7 ps 0.9 t "Sender-1",\
     "./Data/Sender 2.drate" using 1:2 \
     with linespoints pt 9 ps 0.9 t "Sender-2",\
     "./Data/Sender 3.drate" using 1:2 \
     with linespoints pt 13 ps 0.9 t "Sender-3"
pause -1
