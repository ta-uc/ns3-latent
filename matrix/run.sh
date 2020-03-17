#! /bin/bash
declare -A MAP
MAP[0]=A
MAP[1]=B
MAP[2]=C
MAP[3]=D
MAP[4]=E
MAP[5]=F
MAP[6]=G
MAP[7]=H
MAP[8]=I
MAP[9]=J
MAP[10]=K

for i in {0..0}; do
  for j in {10..10}; do
        if [ $i != $j ]; then 
          cd ~/Programs/ns-3-dev/matrix
          python3 ./create_sim.py --OrigNode ${MAP[$i]} --DestNode ${MAP[$j]} >> created
          python3 ./combine.py
          cd ~/Programs/ns-3-dev
          ./waf --run "created --OrigNode=${i} --DestNode=${j}"
          rm capas_incd.py
          rm route.py
          rm created
          # rm created.cc
        fi
    done
done
