set xrange [0:60]
#set yrange [1000000:2000000]
set yrange [50000:8000000]
set key right
set grid

set xlabel 'time'
set ylabel 'datarate'

plot "./n0-n1.drate" using 1:2 \
     with linespoints pt 1 ps 0.9 t "n0-n1",\
     "./n0-n2.drate" using 1:2 \
     with linespoints pt 2 ps 0.9 t "n0-n2",\
     "./n0-n3.drate" using 1:2 \
     with linespoints pt 3 ps 0.9 t "n0-n3",\
     "./n0-n4.drate" using 1:2 \
     with linespoints pt 4 ps 0.9 t "n0-n4",\
     "./n0-n5.drate" using 1:2 \
     with linespoints pt 5 ps 0.9 t "n0-n5",\
     "./n0-n6.drate" using 1:2 \
     with linespoints pt 6 ps 0.9 t "n0-n6",\
     "./n0-n7.drate" using 1:2 \
     with linespoints pt 7 ps 0.9 t "n0-n7",\
     "./n0-n8.drate" using 1:2 \
     with linespoints pt 8 ps 0.9 t "n0-n8",\
     "./n0-n9.drate" using 1:2 \
     with linespoints pt 9 ps 0.9 t "n0-n9",\
     "./n0-n10.drate" using 1:2 \
     with linespoints pt 10 ps 0.9 t "n0-n10"
pause -1
