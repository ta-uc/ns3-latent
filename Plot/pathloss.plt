set xrange [0:60]
set yrange [0:0.3]
set key right
set grid

set xlabel 'time'
set ylabel 'loss'

plot "./n0-n1.loss" using 1:2 \
     with linespoints pt 1 ps 0.9 t "n0-n1",\
     "./n0-n2.loss" using 1:2 \
     with linespoints pt 2 ps 0.9 t "n0-n2",\
     "./n0-n3.loss" using 1:2 \
     with linespoints pt 3 ps 0.9 t "n0-n3",\
     "./n0-n4.loss" using 1:2 \
     with linespoints pt 4 ps 0.9 t "n0-n4",\
     "./n0-n5.loss" using 1:2 \
     with linespoints pt 5 ps 0.9 t "n0-n5",\
     "./n0-n6.loss" using 1:2 \
     with linespoints pt 6 ps 0.9 t "n0-n6",\
     "./n0-n7.loss" using 1:2 \
     with linespoints pt 7 ps 0.9 t "n0-n7",\
     "./n0-n8.loss" using 1:2 \
     with linespoints pt 8 ps 0.9 t "n0-n8",\
     "./n0-n9.loss" using 1:2 \
     with linespoints pt 9 ps 0.9 t "n0-n9",\
     "./n0-n10.loss" using 1:2 \
     with linespoints pt 10 ps 0.9 t "n0-n10"
pause -1