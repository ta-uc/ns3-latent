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
  for j in {1..1}; do
        if [ $i != $j ]; then 
          #before
          cd ~/Programs/ns-3-dev/matrix
          python3 ./create_sim.py --OrigNode ${MAP[$i]} --DestNode ${MAP[$j]} >> created
          python3 ./combine.py
          cd ~/Programs/ns-3-dev
          ./waf --run "created --OrigNode=${i} --DestNode=${j}"
          python3 ./Plot/datarate.py >> ./matrix/odrate.py
          mkdir ./Data/$i-$j-b
          cd ~/Programs/ns-3-dev/matrix
          mv link.* ../Data/$i-$j-b
          mv route.py ../Data/$i-$j-b
          rm created
          rm ../scratch/created.cc

          #latent
          cd ~/Programs/ns-3-dev/matrix
          python3 ./create_sim.py --Opt --OdRate latent --OrigNode ${MAP[$i]} --DestNode ${MAP[$j]} >> created
          python3 ./combine.py
          cd ~/Programs/ns-3-dev
          ./waf --run "created --OrigNode=${i} --DestNode=${j}"
          python3 ./Plot/datarate.py >> ./matrix/odrate_result_latent.py
          mkdir ./Data/$i-$j-l
          cd ~/Programs/ns-3-dev/matrix
          mv link.* ../Data/$i-$j-l
          mv capas_incd.py ../Data/$i-$j-l
          mv route.py ../Data/$i-$j-l
          rm created
          rm ../scratch/created.cc

          #actual
          cd ~/Programs/ns-3-dev/matrix
          python3 ./create_sim.py --Opt --OdRate actual --OrigNode ${MAP[$i]} --DestNode ${MAP[$j]} >> created
          python3 ./combine.py
          cd ~/Programs/ns-3-dev
          ./waf --run "created --OrigNode=${i} --DestNode=${j}"
          python3 ./Plot/datarate.py >> ./matrix/odrate_result_actual.py
          mkdir ./Data/$i-$j-a
          cd ~/Programs/ns-3-dev/matrix
          mv link.* ../Data/$i-$j-a
          mv capas_incd.py ../Data/$i-$j-a
          mv route.py ../Data/$i-$j-a
          rm created
          rm ../scratch/created.cc
          mv odrate_result_actual.py ../Data/$i-$j-a
          mv odrate_result_latent.py ../Data/$i-$j-l
          mv odrate.py ../Data/$i-$j-b
        fi
    done
done
